/* minmax-test: sampler min/max reduction cases the Vulkan CTS does not reach, on a 4x4 R8 texture
 * with three mip levels of known texels, sampled from a compute shader:
 *   - an explicit LOD outside the sampler's LOD clamp;
 *   - nearest filtering with linear mip filtering (two texels, one per level);
 *   - a sparse sampling operation;
 * and the extension enabled without the Vulkan 1.2 feature bit.
 *
 *   tools/minmax-test/run.sh  builds the shader and this program, then runs it on KosmicKrisp
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "minmax-test-cs.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)

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

static VkBuffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, void **map)
{
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage };
    VkBuffer buf;
    CHECK(vkCreateBuffer(dev, &info, NULL, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size,
            memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory mem;
    CHECK(vkAllocateMemory(dev, &alloc, NULL, &mem));
    CHECK(vkBindBufferMemory(dev, buf, mem, 0));
    CHECK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, map));
    return buf;
}

static VkSampler make_sampler(VkFilter filter, VkSamplerMipmapMode mip, float max_lod, VkSamplerReductionMode mode)
{
    VkSamplerReductionModeCreateInfo red = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO, NULL, mode };
    VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, &red, 0, filter, filter, mip,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE };
    info.maxLod = max_lod;
    VkSampler s;
    CHECK(vkCreateSampler(dev, &info, NULL, &s));
    return s;
}

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "minmax-test", 1, NULL, 0, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, &pdev) < 0);

    /* The extension alone, as a Vulkan 1.1 application enables it: no feature structure. */
    VkPhysicalDeviceFeatures features = { .shaderResourceResidency = VK_TRUE };
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    const char *exts[] = { VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 1, exts, &features };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* Level 0 holds 10 * (x + 4y), level 1 holds 200, 210, 220, 230, level 2 holds 250. */
    VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM,
            { 4, 4, 1 }, 3, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    VkImage image;
    CHECK(vkCreateImage(dev, &imci, NULL, &image));
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(dev, image, &ireq);
    VkMemoryAllocateInfo ialloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, ireq.size,
            memory_type(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory imem;
    CHECK(vkAllocateMemory(dev, &ialloc, NULL, &imem));
    CHECK(vkBindImageMemory(dev, image, imem, 0));
    VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, image, VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R8_UNORM, {0}, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 0, 1 } };
    VkImageView view;
    CHECK(vkCreateImageView(dev, &ivci, NULL, &view));

    uint8_t *texels;
    VkBuffer staging = make_buffer(64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, (void **)&texels);
    for (int i = 0; i < 16; i++)
        texels[i] = 10 * i;
    for (int i = 0; i < 4; i++)
        texels[16 + i] = 200 + 10 * i;
    texels[20] = 250;
    float *out;
    VkBuffer out_buf = make_buffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void **)&out);

    VkSampler samplers[5] = {
        make_sampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, VK_SAMPLER_REDUCTION_MODE_MAX),
        make_sampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR, 1000.0f, VK_SAMPLER_REDUCTION_MODE_MAX),
        make_sampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR, 1000.0f, VK_SAMPLER_REDUCTION_MODE_MIN),
        make_sampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, 1000.0f, VK_SAMPLER_REDUCTION_MODE_MAX),
        make_sampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, 1000.0f, VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE),
    };

    VkDescriptorSetLayoutBinding bindings[6];
    for (int i = 0; i < 6; i++)
        bindings[i] = (VkDescriptorSetLayoutBinding){ i, i < 5 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                1, VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 6, bindings };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
    VkDescriptorPoolSize sizes[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 2, sizes };
    VkDescriptorPool dpool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 1, &dsl };
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorImageInfo images[5];
    VkWriteDescriptorSet writes[6];
    for (int i = 0; i < 5; i++) {
        images[i] = (VkDescriptorImageInfo){ samplers[i], view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        writes[i] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, i, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[i] };
    }
    VkDescriptorBufferInfo out_info = { out_buf, 0, VK_WHOLE_SIZE };
    writes[5] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 5, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &out_info };
    vkUpdateDescriptorSets(dev, 6, writes, 0, NULL);

    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(minmax_test_cs), minmax_test_cs };
    VkShaderModule module;
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &module));
    VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main" }, pl };
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));

    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    VkImageMemoryBarrier to_dst = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0, image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 0, 1 } };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_dst);
    VkBufferImageCopy copies[3] = {
        { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0 }, { 4, 4, 1 } },
        { 16, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1 }, { 0 }, { 2, 2, 1 } },
        { 20, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, 1 }, { 0 }, { 1, 1, 1 } },
    };
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 3, copies);
    VkImageMemoryBarrier to_read = to_dst;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &to_read);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, 1, 1, 1);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

    static const struct { const char *name; float want; } cases[8] = {
        { "explicit LOD 1 clamped to level 0 by the sampler, max", 100 },
        { "nearest filter, linear mip, max", 200 },
        { "nearest filter, linear mip, min", 50 },
        { "sparse sample, max", 100 },
        { "sparse sample, resident", 255 },
        { "plain sample, max", 100 },
        { "two levels, max", 230 },
        { "weighted average", 75 },
    };
    int failures = 0;
    for (int i = 0; i < 8; i++) {
        float got = out[i] * 255.0f;
        int ok = fabsf(got - cases[i].want) < 0.75f;
        printf("%-56s want %5.1f got %5.1f %s\n", cases[i].name, cases[i].want, got, ok ? "" : "FAIL");
        failures += !ok;
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
