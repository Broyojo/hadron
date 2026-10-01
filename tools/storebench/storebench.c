/* storebench: time of a compute shader dominated by storage buffer stores, to price the
 * residency guard KosmicKrisp puts on stores when sparseResidencyBuffer is enabled. Runs each
 * case on a device with the feature off, with it on, and with it on while a sparse residency
 * buffer exists on the device; the buffers written are ordinary (not sparse).
 *
 *   tools/storebench/run.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#include "storebench-cs.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)
#define THREADS (1u << 22)
#define RUNS 21

static VkPhysicalDevice pdev;

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    exit(1);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void run(int sparse_feature, int sparse_buffer, double out[3])
{
    VkPhysicalDeviceFeatures features = {0};
    features.sparseBinding = features.sparseResidencyBuffer = sparse_feature;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 0, NULL, &features };
    VkDevice dev;
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    if (sparse_buffer) {
        VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL,
                VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT, 1 << 20,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        VkBuffer sparse;
        CHECK(vkCreateBuffer(dev, &info, NULL, &sparse));
    }

    VkBuffer bufs[2];
    VkDeviceSize sizes[2] = { (VkDeviceSize)THREADS * 16 * 4, 16 };
    for (int i = 0; i < 2; i++) {
        VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sizes[i], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        CHECK(vkCreateBuffer(dev, &info, NULL, &bufs[i]));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, bufs[i], &req);
        VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size,
                memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        VkDeviceMemory mem;
        CHECK(vkAllocateMemory(dev, &alloc, NULL, &mem));
        CHECK(vkBindBufferMemory(dev, bufs[i], mem, 0));
    }

    VkDescriptorSetLayoutBinding bindings[2] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
    };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, bindings };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
    VkDescriptorPoolSize psize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &psize };
    VkDescriptorPool dpool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 1, &dsl };
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorBufferInfo infos[2] = { { bufs[0], 0, VK_WHOLE_SIZE }, { bufs[1], 0, VK_WHOLE_SIZE } };
    VkWriteDescriptorSet writes[2] = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &infos[0] },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &infos[1] },
    };
    vkUpdateDescriptorSets(dev, 2, writes, 0, NULL);

    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(storebench_cs), storebench_cs };
    VkShaderModule module;
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &module));

    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));

    for (int mode = 0; mode < 3; mode++) {
        VkSpecializationMapEntry entry = { 0, 0, 4 };
        VkSpecializationInfo spec = { 1, &entry, 4, &mode };
        VkComputePipelineCreateInfo pci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
                { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", &spec }, pl };
        VkPipeline pipeline;
        CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipeline));

        double times[RUNS];
        for (int r = 0; r < RUNS + 3; r++) {
            CHECK(vkResetCommandBuffer(cmd, 0));
            VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            CHECK(vkBeginCommandBuffer(cmd, &cbbi));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
            vkCmdDispatch(cmd, THREADS / 64, 1, 1);
            CHECK(vkEndCommandBuffer(cmd));
            VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
            double t0 = now_ms();
            CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
            CHECK(vkQueueWaitIdle(queue));
            if (r >= 3)
                times[r - 3] = now_ms() - t0;
        }
        qsort(times, RUNS, sizeof(double), cmp);
        out[mode] = times[RUNS / 2];
        vkDestroyPipeline(dev, pipeline, NULL);
    }
    vkDeviceWaitIdle(dev);
    vkDestroyDevice(dev, NULL);
}

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "storebench", 1, NULL, 0, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, &pdev) < 0);

    static const char *names[3] = { "64M stores", "64M load + store", "16M atomic adds" };
    double off[2][3], on[2][3], live[2][3];
    for (int r = 0; r < 2; r++) {
        run(0, 0, off[r]);
        run(1, 0, on[r]);
        run(1, 1, live[r]);
    }
    printf("%-22s %10s %18s %22s\n", "ms, submit to idle", "feature off", "on, no sparse buffer", "on, sparse buffer alive");
    for (int i = 0; i < 3; i++) {
        double a = (off[0][i] + off[1][i]) / 2, b = (on[0][i] + on[1][i]) / 2, c = (live[0][i] + live[1][i]) / 2;
        printf("%-22s %10.3f %10.3f (%+5.1f%%) %12.3f (%+5.1f%%)\n", names[i], a, b, (b - a) / a * 100, c, (c - a) / a * 100);
    }
    return 0;
}
