/* dgc-test3: a DGC indexed draw of two triangle strips separated by a primitive restart index,
 * through a pass-through geometry shader. The strips are quads 0 and 2; if the driver joins them
 * across the restart, the triangles between them cover quad 1's rectangle.
 *
 *   tools/dgc-test/run.sh 3
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "dgc-test3-vs.h"
#include "dgc-test3-fs.h"
#include "dgc-test3-gs.h"

#define W 64
#define H 64
#define SEQS 1
#define MAX_SEQS 4
#define STRIDE 80

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)

static VkDevice dev;
static VkPhysicalDevice pdev;

struct buf { VkBuffer buf; VkDeviceMemory mem; void *map; VkDeviceAddress addr; };

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fprintf(stderr, "no memory type\n");
    exit(1);
}

static struct buf make_buffer2(VkDeviceSize size, VkBufferUsageFlags2 usage2);

static struct buf make_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    return make_buffer2(size, usage);
}

static struct buf make_buffer2(VkDeviceSize size, VkBufferUsageFlags2 usage2)
{
    struct buf b = {0};
    VkBufferUsageFlags2CreateInfo usage_info = { VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO, NULL,
            usage2 | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT };
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &usage_info, 0, size,
            0, VK_SHARING_MODE_EXCLUSIVE };
    CHECK(vkCreateBuffer(dev, &info, NULL, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, b.buf, &req);
    VkMemoryAllocateFlagsInfo flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL,
            VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT };
    VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags, req.size,
            memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    CHECK(vkAllocateMemory(dev, &alloc, NULL, &b.mem));
    CHECK(vkBindBufferMemory(dev, b.buf, b.mem, 0));
    CHECK(vkMapMemory(dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
    memset(b.map, 0, size);
    VkBufferDeviceAddressInfo ai = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, b.buf };
    b.addr = vkGetBufferDeviceAddress(dev, &ai);
    return b;
}

static VkShaderModule shader(const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, size, code };
    VkShaderModule m;
    CHECK(vkCreateShaderModule(dev, &info, NULL, &m));
    return m;
}

/* Quad i covers pixel columns [16i + 2, 16i + 14) and rows [2, 62). */
static void quad(float *v, int i)
{
    float x0 = (16 * i + 2) / 32.0f - 1, x1 = (16 * i + 14) / 32.0f - 1, y0 = 2 / 32.0f - 1, y1 = 62 / 32.0f - 1;
    float q[8] = { x0, y0, x1, y0, x0, y1, x1, y1 };
    memcpy(v, q, sizeof(q));
}

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "dgc-test", 1, NULL, 0, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, &pdev) < 0);

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            NULL, VK_TRUE, VK_TRUE, VK_TRUE };
    VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT dgc_features = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT, &robustness2, VK_TRUE };
    VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &dgc_features };
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13 };
    f12.bufferDeviceAddress = VK_TRUE;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    const char *exts[] = { VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            VK_KHR_MAINTENANCE_5_EXTENSION_NAME };
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
    f2.features.robustBufferAccess = VK_TRUE;
    f2.features.geometryShader = VK_TRUE;
    VkPhysicalDeviceMaintenance5Features m5 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES, f12.pNext, VK_TRUE };
    f12.pNext = &m5;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qci, 0, NULL, 3, exts };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    PFN_vkCreateIndirectCommandsLayoutEXT create_layout = (void *)vkGetDeviceProcAddr(dev, "vkCreateIndirectCommandsLayoutEXT");
    PFN_vkGetGeneratedCommandsMemoryRequirementsEXT mem_req = (void *)vkGetDeviceProcAddr(dev, "vkGetGeneratedCommandsMemoryRequirementsEXT");
    PFN_vkCmdExecuteGeneratedCommandsEXT execute = (void *)vkGetDeviceProcAddr(dev, "vkCmdExecuteGeneratedCommandsEXT");

    /* Render target and readback. */
    VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
            { W, H, 1 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
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
            VK_FORMAT_R8G8B8A8_UNORM, {0}, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView view;
    CHECK(vkCreateImageView(dev, &ivci, NULL, &view));
    struct buf readback = make_buffer(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    /* Geometry: vertex buffer A holds quads 0 and 1 (quad 1 drawn with base vertex 4), B holds 2 and 3. */
    struct buf vb_a = make_buffer(256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    struct buf vb_b = make_buffer(256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    quad((float *)vb_a.map, 0);
    quad((float *)vb_a.map + 8, 2);
    quad((float *)vb_b.map, 2);
    quad((float *)vb_b.map + 8, 3);
    /* 16-bit indices, the second copy after 3 padding indices (drawn with firstIndex 3); 32-bit indices. */
    struct buf ib16 = make_buffer(256, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    struct buf ib32 = make_buffer(256, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    uint16_t i16[] = { 0, 1, 2, 3, 0xffff, 4, 5, 6, 7 };
    uint32_t i32[] = { 4, 5, 6, 6, 5, 7 };
    memcpy(ib16.map, i16, sizeof(i16));
    memcpy(ib32.map, i32, sizeof(i32));

    /* Pipeline. */
    VkPushConstantRange pcr = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 1, &pcr };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkPipelineShaderStageCreateInfo stages[3] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT,
          shader(dgc_test3_vs, sizeof(dgc_test3_vs)), "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_GEOMETRY_BIT,
          shader(dgc_test3_gs, sizeof(dgc_test3_gs)), "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
          shader(dgc_test3_fs, sizeof(dgc_test3_fs)), "main" },
    };
    VkVertexInputBindingDescription vbd = { 0, 8, VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription vad = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 1, &vbd, 1, &vad };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_TRUE };
    VkViewport vp = { 0, 0, W, H, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, &vp, 1, &sc };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0, 1, &cba };
    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
    VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 1, dyn_states };
    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo prci = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &fmt };
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &prci, 0, 3, stages, &vi, &ia,
            NULL, &vps, &rs, &ms, NULL, &cb, &dyn, pl };
    VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));

    /* Indirect commands layout: color @0, vertex buffer @16, index buffer @32, draw @48. */
    VkIndirectCommandsPushConstantTokenEXT pc_token = { pcr };
    VkIndirectCommandsVertexBufferTokenEXT vb_token = { 0 };
    VkIndirectCommandsIndexBufferTokenEXT ib_token = { VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT };
    VkIndirectCommandsLayoutTokenEXT tokens[4] = {
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT, { .pPushConstant = &pc_token }, 0 },
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT, { .pVertexBuffer = &vb_token }, 16 },
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT, { .pIndexBuffer = &ib_token }, 32 },
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT, { NULL }, 48 },
    };
    VkIndirectCommandsLayoutCreateInfoEXT lci = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT, NULL, 0,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, STRIDE, pl, 4, tokens };
    VkIndirectCommandsLayoutEXT layout;
    CHECK(create_layout(dev, &lci, NULL, &layout));

    /* Stream. */
    /* MAX_SEQS sequences, of which a count buffer enables SEQS; the rest would draw garbage. */
    struct buf stream = make_buffer(STRIDE * MAX_SEQS, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    memset(stream.map, 0x5a, STRIDE * MAX_SEQS);
    struct buf count = make_buffer(16, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    *(uint32_t *)count.map = SEQS;
    static const float colors[SEQS][4] = { { 1, 0, 0, 1 } };
    for (int s = 0; s < SEQS; s++)
    {
        uint8_t *seq = (uint8_t *)stream.map + s * STRIDE;
        struct { uint64_t va; uint32_t size, stride; } vbv = { s < 2 ? vb_a.addr : vb_b.addr, 64, 8 };
        struct { uint64_t va; uint32_t size, format; } ibv = { ib16.addr, 18, 57 };
        VkDrawIndexedIndirectCommand draw = { 9, 1, 0, 0, 0 };
        if (s == 2)
            draw.vertexOffset = 0;
        memcpy(seq, colors[s], 16);
        memcpy(seq + 16, &vbv, 16);
        memcpy(seq + 32, &ibv, 16);
        memcpy(seq + 48, &draw, sizeof(draw));
    }

    VkGeneratedCommandsPipelineInfoEXT gpi = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT, NULL, pipeline };
    VkGeneratedCommandsMemoryRequirementsInfoEXT mri = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_EXT,
            &gpi, VK_NULL_HANDLE, layout, MAX_SEQS, 0 };
    VkMemoryRequirements2 mr = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    mem_req(dev, &mri, &mr);
    struct buf pre = make_buffer2(mr.memoryRequirements.size, VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT);
    printf("preprocess size %llu\n", (unsigned long long)mr.memoryRequirements.size);

    /* Record. */
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));

    VkImageMemoryBarrier2 to_rt = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, NULL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, NULL, 0, 0, NULL, 0, NULL, 1, &to_rt };
    vkCmdPipelineBarrier2(cmd, &dep);

    VkRenderingAttachmentInfo att = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, { { 0, 0 }, { W, H } }, 1, 0, 1, &att };
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkGeneratedCommandsInfoEXT gci = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_EXT, &gpi,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, layout,
            stream.addr, STRIDE * MAX_SEQS, pre.addr, mr.memoryRequirements.size, MAX_SEQS, count.addr, 0 };
    execute(cmd, VK_FALSE, &gci);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 to_src = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, NULL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0, image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    dep.pImageMemoryBarriers = &to_src;
    vkCmdPipelineBarrier2(cmd, &dep);
    VkBufferImageCopy copy = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { W, H, 1 } };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buf, 1, &copy);
    CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

    /* Quads 0 and 2 should be red, and quad 1's rectangle between them untouched. */
    const uint8_t *px = readback.map;
    int failures = 0;
    for (int q = 0; q < 3; q++)
    {
        int drawn = 0, total = 0;
        for (int y = 2; y < 62; y++)
            for (int x = 16 * q + 2; x < 16 * q + 14; x++, total++)
                drawn += px[(y * W + x) * 4 + 3] != 0;
        printf("quad %d: %d/%d pixels drawn (want %d)\n", q, drawn, total, q == 1 ? 0 : total);
        failures += drawn != (q == 1 ? 0 : total);
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
