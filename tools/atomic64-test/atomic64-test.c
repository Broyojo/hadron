/* atomic64-test: 64-bit atomic operations on storage buffers and r64ui images under contention.
 * Metal has only 64-bit atomic min and max without a result, so KosmicKrisp runs every other
 * operation under a lock (kk_nir_lower_atomic64.c); up to 65536 threads share 1, 64 or 4096 words
 * or texels and the totals must be exact. The max-only pipeline runs before any other is created
 * (Metal's own atomic) and again after (the locked path that keeps it atomic against the other
 * operations).
 *
 *   tools/atomic64-test/run.sh  builds the shader and this program, then runs it on KosmicKrisp
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#include "atomic64-test-cs.h"
#include "atomic64-image-cs.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)
#define THREADS 65536u
#define MAX_WORDS 4096u

static VkDevice dev;
static VkPhysicalDevice pdev;
static VkQueue queue;
static VkCommandBuffer cmd;
static VkPipelineLayout pl;
static VkDescriptorSet set;
static VkShaderModule module, image_module;
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

static VkPipeline make_pipeline(VkShaderModule module, uint32_t mode)
{
    VkSpecializationMapEntry entry = { 0, 0, 4 };
    VkSpecializationInfo spec = { 1, &entry, 4, &mode };
    VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", &spec }, pl };
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
    return pipeline;
}

/* Threads that share a word run one after another. Keep each dispatch short: macOS aborts GPU
 * work that holds up the display for about 40 ms, and that loses the device. */
static uint32_t threads_for(uint32_t mode, uint32_t count)
{
    /* a compare-and-swap loop retries once for every other thread on its word */
    uint32_t per_word = mode == 2 ? 64u : 256u;
    return count * per_word < THREADS ? count * per_word : THREADS;
}

/* in_image: fill the image, run the operation on it, then copy it to the buffer to check */
static double run(VkPipeline pipeline, uint32_t mode, uint32_t count, uint64_t fill, VkPipeline fill_image,
                  VkPipeline read_image)
{
    for (uint32_t i = 0; i < 2 * MAX_WORDS; i++)
        words[i] = i < count && !fill_image ? fill : 0;
    uint32_t push[3] = { count, (uint32_t)fill, (uint32_t)(fill >> 32) };
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, push);
    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_SHADER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
    if (fill_image) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fill_image);
        vkCmdDispatch(cmd, 4096 / 64, 1, 1);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0,
                             NULL, 0, NULL);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdDispatch(cmd, threads_for(mode, count) / 64, 1, 1);
    if (read_image) {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0,
                             NULL, 0, NULL);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, read_image);
        vkCmdDispatch(cmd, 4096 / 64, 1, 1);
    }
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
static int check(uint32_t mode, uint32_t count, uint32_t sums)
{
    uint32_t bad = 0;
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
            want = all; got = words[w] + words[sums + w];
            break;
        }
        case 5: want = (uint64_t)w + (7ull << 32); break;
        }
        if (got != want) {
            if (!bad++)
                printf("      word %u: got %016llx want %016llx\n", w, (unsigned long long)got, (unsigned long long)want);
        }
    }
    if (bad)
        printf("      %u of %u words wrong\n", bad, count);
    return !bad;
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
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT fi = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT };
    VkPhysicalDeviceFeatures2 fi2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &fi };
    vkGetPhysicalDeviceFeatures2(pdev, &fi2);
    printf("shaderBufferInt64Atomics %d, shaderImageInt64Atomics %d\n", f12.shaderBufferInt64Atomics, fi.shaderImageInt64Atomics);
    if (!f12.shaderBufferInt64Atomics || !fi.shaderImageInt64Atomics)
        return 1;
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT ei = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT };
    ei.shaderImageInt64Atomics = VK_TRUE;
    VkPhysicalDeviceVulkan12Features e12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &ei };
    e12.shaderBufferInt64Atomics = VK_TRUE;
    VkPhysicalDeviceFeatures2 e2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &e12 };
    e2.features.shaderInt64 = VK_TRUE;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    const char *exts[] = { VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &e2, 0, 1, &qci, 0, NULL, 1, exts };
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

    /* a 64x64 r64ui image, one texel per word */
    VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R64_UINT, { 64, 64, 1 }, 1, 1,
            VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT };
    VkImage image;
    CHECK(vkCreateImage(dev, &imci, NULL, &image));
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(dev, image, &ireq);
    VkMemoryAllocateInfo ialloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, ireq.size,
            memory_type(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory imem;
    CHECK(vkAllocateMemory(dev, &ialloc, NULL, &imem));
    CHECK(vkBindImageMemory(dev, image, imem, 0));
    VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R64_UINT,
            { 0 }, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView image_view;
    CHECK(vkCreateImageView(dev, &ivci, NULL, &image_view));

    VkDescriptorSetLayoutBinding binding[2] = { { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
                                                { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT } };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, binding };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
    VkDescriptorPoolSize sizes[2] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 } };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 2, sizes };
    VkDescriptorPool dpool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 1, &dsl };
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorBufferInfo info = { buf, 0, VK_WHOLE_SIZE };
    VkDescriptorImageInfo image_info = { VK_NULL_HANDLE, image_view, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet write[2] = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &info },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &image_info },
    };
    vkUpdateDescriptorSets(dev, 2, write, 0, NULL);
    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 12 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 1, &pcr };
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(atomic64_test_cs), atomic64_test_cs };
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &module));
    VkShaderModuleCreateInfo ismci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(atomic64_image_cs), atomic64_image_cs };
    CHECK(vkCreateShaderModule(dev, &ismci, NULL, &image_module));
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));

    /* the image stays in the general layout */
    VkCommandBufferBeginInfo lbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &lbbi));
    VkImageMemoryBarrier to_general = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &to_general);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo lsi = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(queue, 1, &lsi, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

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
    for (int in_image = 0; in_image < 2; in_image++) {
        VkShaderModule m = in_image ? image_module : module;
        VkPipeline max_pipeline = make_pipeline(m, 0);
        VkPipeline fill_image = in_image ? make_pipeline(m, 100) : VK_NULL_HANDLE;
        VkPipeline read_image = in_image ? make_pipeline(m, 101) : VK_NULL_HANDLE;
        printf("%s\n", in_image ? "r64ui image" : "storage buffer");
        for (unsigned t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
            VkPipeline pipeline = tests[t].mode == 0 ? max_pipeline : make_pipeline(m, tests[t].mode);
            for (unsigned c = 0; c < 3; c++) {
                double ms = run(pipeline, tests[t].mode, counts[c], tests[t].fill, fill_image, read_image);
                int ok = check(tests[t].mode, counts[c], in_image ? MAX_WORDS : counts[c]);
                printf("  %-32s %5u threads on %4u: %8.2f ms  %s\n", tests[t].name, threads_for(tests[t].mode, counts[c]),
                       counts[c], ms, ok ? "exact" : "FAIL");
                failures += !ok;
            }
        }
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
