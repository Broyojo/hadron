/* sparse-test: strict behaviour of a sparse-residency buffer with pages 0 and 2 of 4 bound.
 * A compute shader writes, then reads back through volatile memory (Metal alone returns what was
 * written to an unbound page until the command buffer ends):
 *   - a storage buffer store and atomic to an unbound page are discarded;
 *   - a vector store straddling a bound and an unbound page writes only its bound part;
 *   - a store through a device address to an unbound page is discarded.
 *
 *   tools/sparse-test/run.sh  builds the shaders and this program, then runs it on KosmicKrisp
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "sparse-test-cs.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)
#define PAGE 65536

static VkDevice dev;
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

static VkPipeline make_pipeline(const uint32_t *code, size_t size, VkPipelineLayout pl)
{
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, size, code };
    VkShaderModule module;
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &module));
    VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main" }, pl };
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
    return pipeline;
}

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "sparse-test", 1, NULL, 0, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, &pdev) < 0);

    VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    f12.bufferDeviceAddress = f12.scalarBlockLayout = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
    f2.features.sparseBinding = f2.features.sparseResidencyBuffer = VK_TRUE;
    f2.features.shaderInt64 = VK_TRUE;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qci };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* The sparse buffer, with memory behind pages 0 and 2. */
    VkBufferCreateInfo sbci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL,
            VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT, 4 * PAGE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
    VkBuffer sparse;
    CHECK(vkCreateBuffer(dev, &sbci, NULL, &sparse));
    VkMemoryRequirements sreq;
    vkGetBufferMemoryRequirements(dev, sparse, &sreq);
    VkMemoryAllocateInfo salloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, 2 * PAGE,
            memory_type(sreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory smem;
    CHECK(vkAllocateMemory(dev, &salloc, NULL, &smem));
    VkSparseMemoryBind binds[2] = { { 0, PAGE, smem, 0 }, { 2 * PAGE, PAGE, smem, PAGE } };
    VkSparseBufferMemoryBindInfo bbi = { sparse, 2, binds };
    VkBindSparseInfo bsi = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO, NULL, 0, NULL, 1, &bbi };
    CHECK(vkQueueBindSparse(queue, 1, &bsi, VK_NULL_HANDLE));
    VkBufferDeviceAddressInfo ai = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, sparse };
    uint64_t address = vkGetBufferDeviceAddress(dev, &ai);

    VkBufferCreateInfo obci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, 128, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer out_buf;
    CHECK(vkCreateBuffer(dev, &obci, NULL, &out_buf));
    VkMemoryRequirements oreq;
    vkGetBufferMemoryRequirements(dev, out_buf, &oreq);
    VkMemoryAllocateInfo oalloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, oreq.size,
            memory_type(oreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory omem;
    CHECK(vkAllocateMemory(dev, &oalloc, NULL, &omem));
    CHECK(vkBindBufferMemory(dev, out_buf, omem, 0));
    uint32_t *out;
    CHECK(vkMapMemory(dev, omem, 0, VK_WHOLE_SIZE, 0, (void **)&out));
    memset(out, 0xee, 128);

    VkDescriptorSetLayoutBinding bindings[3] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
    };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 3, bindings };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
    VkDescriptorPoolSize sizes = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &sizes };
    VkDescriptorPool dpool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 1, &dsl };
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorBufferInfo sparse_info = { sparse, 0, VK_WHOLE_SIZE }, out_info = { out_buf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet writes[3] = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &sparse_info },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &sparse_info },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &out_info },
    };
    vkUpdateDescriptorSets(dev, 3, writes, 0, NULL);

    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 8 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 1, &pcr };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkPipeline write_pipeline = make_pipeline(sparse_test_cs, sizeof(sparse_test_cs), pl);

    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdFillBuffer(cmd, sparse, 0, 4 * PAGE, 0);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, &address);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, write_pipeline);
    vkCmdDispatch(cmd, 1, 1, 1);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

    static const struct { const char *name; uint32_t want; } cases[9] = {
        { "storage buffer store to a bound page", 0x11 },
        { "storage buffer store to an unbound page", 0 },
        { "straddling store, component 0 (bound)", 0xa0 },
        { "straddling store, component 1 (bound)", 0xa1 },
        { "straddling store, component 2 (unbound)", 0 },
        { "straddling store, component 3 (unbound)", 0 },
        { "device address store to a bound page", 0x22 },
        { "device address store to an unbound page", 0 },
        { "atomic add on an unbound page", 0 },
    };
    int failures = 0;
    for (int i = 0; i < 9; i++) {
        int ok = out[i] == cases[i].want;
        printf("%-42s want %#6x got %#6x %s\n", cases[i].name, cases[i].want, out[i], ok ? "" : "FAIL");
        failures += !ok;
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
