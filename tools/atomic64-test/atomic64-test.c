/* atomic64-test: 64-bit atomic operations on storage buffers under contention. Metal has only
 * 64-bit atomic min and max without a result, so KosmicKrisp runs every other operation under a
 * lock (kk_nir_lower_atomic64.c); up to 65536 threads share 1, 64 or 4096 words and the totals
 * must be exact. The max-only pipeline runs before any other is created (Metal's own atomic) and again
 * after (the locked path that keeps it atomic against the other operations).
 *
 *   tools/atomic64-test/run.sh  builds the shader and this program, then runs it on KosmicKrisp
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#include "atomic64-test-cs.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)
#define THREADS 65536u
#define MAX_WORDS 4096u

static VkDevice dev;
static VkPhysicalDevice pdev;
static VkQueue queue;
static VkCommandBuffer cmd;
static VkPipelineLayout pl;
static VkDescriptorSet set;
static VkShaderModule module;
static uint64_t *words;

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    exit(1);
}

static VkPipeline make_pipeline(uint32_t mode)
{
    VkSpecializationMapEntry entry = { 0, 0, 4 };
    VkSpecializationInfo spec = { 1, &entry, 4, &mode };
    VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", &spec }, pl };
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
    return pipeline;
}

/* One word shared by every thread makes the whole dispatch serial; keep that case short, the
 * system aborts GPU work that holds up the display. */
static uint32_t threads_for(uint32_t mode, uint32_t count)
{
    /* a compare-and-swap loop retries once for every other thread on its word */
    uint32_t per_word = mode == 2 ? 64u : 2048u;
    return count * per_word < THREADS ? count * per_word : THREADS;
}

static double run(VkPipeline pipeline, uint32_t mode, uint32_t count, uint64_t fill)
{
    for (uint32_t i = 0; i < 2 * MAX_WORDS; i++)
        words[i] = i < count ? fill : 0;
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &count);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdDispatch(cmd, threads_for(mode, count) / 64, 1, 1);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

/* what every word must hold once the threads, thread t on word t % count, have run */
static int check(uint32_t mode, uint32_t count)
{
    for (uint32_t w = 0; w < count; w++) {
        uint64_t per = threads_for(mode, count) / count, want = 0, got = words[w];
        switch (mode) {
        case 0: want = (uint64_t)(w + (per - 1) * count) + 1; break;
        case 1: case 2: want = per * 0x100000001ull; break;
        case 3: for (uint64_t k = 0; k < per; k++) want ^= 1ull << ((w + k * count) % 64); break;
        case 4: {
            /* the word holds one thread's token, the sum word the others': together, all of them */
            uint64_t all = 0;
            for (uint64_t k = 0; k < per; k++) all += (w + k * count) + 1;
            want = all; got = words[w] + words[count + w];
            break;
        }
        case 5: want = (uint64_t)w + (7ull << 32); break;
        }
        if (got != want) {
            printf("      word %u: got %016llx want %016llx\n", w, (unsigned long long)got, (unsigned long long)want);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "atomic64-test", 1, NULL, 0, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, &pdev) < 0);

    VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
    vkGetPhysicalDeviceFeatures2(pdev, &f2);
    printf("shaderBufferInt64Atomics %d\n", f12.shaderBufferInt64Atomics);
    if (!f12.shaderBufferInt64Atomics)
        return 1;
    VkPhysicalDeviceVulkan12Features e12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    e12.shaderBufferInt64Atomics = VK_TRUE;
    VkPhysicalDeviceFeatures2 e2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &e12 };
    e2.features.shaderInt64 = VK_TRUE;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &e2, 0, 1, &qci };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, 2 * MAX_WORDS * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer buf;
    CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size,
            memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory mem;
    CHECK(vkAllocateMemory(dev, &alloc, NULL, &mem));
    CHECK(vkBindBufferMemory(dev, buf, mem, 0));
    CHECK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&words));

    VkDescriptorSetLayoutBinding binding = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &binding };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
    VkDescriptorPoolSize sizes = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &sizes };
    VkDescriptorPool dpool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 1, &dsl };
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorBufferInfo info = { buf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &info };
    vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);
    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 1, &pcr };
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(atomic64_test_cs), atomic64_test_cs };
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &module));
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));

    static const struct { const char *name; uint32_t mode; uint64_t fill; } tests[] = {
        { "max, before any locked pipeline", 0, 0 },
        { "add", 1, 0 },
        { "add by compare-and-swap", 2, 0 },
        { "xor", 3, 0 },
        { "exchange", 4, 0 },
        { "min", 5, ~0ull },
        { "max, after locked pipelines", 0, 0 },
    };
    static const uint32_t counts[] = { 1, 64, 4096 };
    int failures = 0;
    VkPipeline max_pipeline = make_pipeline(0);
    for (unsigned t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        VkPipeline pipeline = tests[t].mode == 0 ? max_pipeline : make_pipeline(tests[t].mode);
        for (unsigned c = 0; c < 3; c++) {
            double ms = run(pipeline, tests[t].mode, counts[c], tests[t].fill);
            int ok = check(tests[t].mode, counts[c]);
            printf("%-34s %5u threads on %4u words: %8.2f ms  %s\n", tests[t].name, threads_for(tests[t].mode, counts[c]), counts[c], ms,
                   ok ? "exact" : "FAIL");
            failures += !ok;
        }
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
