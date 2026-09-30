/* dgc-test2: the other two ways Teardown uses D3D12 ExecuteIndirect through vkd3d-proton, in the
 * middle of a render pass with a depth buffer:
 *   A: root constant buffer address + non-indexed draw, geometry built from the vertex index;
 *   B: root constant buffer address + indexed draw from the index buffer bound on the CPU.
 * A normal draw before the executes (background, far) and one after (curtain, between) check that
 * depth and color survive the render pass splits DGC processing causes.
 *
 *   tools/dgc-test/run.sh 2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "dgc-test2-vs.h"
#include "dgc-test2-fs.h"
#include "dgc-test2-diag-vs.h"

#define W 64
#define H 64
#define SA (getenv("A_STRIDE") ? atoi(getenv("A_STRIDE")) : 24)

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

static struct buf make_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    struct buf b = {0};
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size,
            usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_SHARING_MODE_EXCLUSIVE };
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

struct obj { float rect[4]; float color[4]; float z; float pad[3]; };

static const float colors[4][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 0, 1 } };

/* Object i covers pixel columns [16i + 2, 16i + 14) and rows [2, 62) at depth 0.5. */
static void make_obj(struct obj *o, int i)
{
    struct obj v = { { (16 * i + 2) / 32.0f - 1, 2 / 32.0f - 1, (16 * i + 14) / 32.0f - 1, 62 / 32.0f - 1 },
            { colors[i][0], colors[i][1], colors[i][2], colors[i][3] }, 0.5f };
    *o = v;
}

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "dgc-test2", 1, NULL, 0, VK_API_VERSION_1_3 };
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
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
    f2.features.robustBufferAccess = !getenv("NO_ROBUST");
    if (getenv("NO_ROBUST"))
        robustness2.robustBufferAccess2 = robustness2.robustImageAccess2 = VK_FALSE;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    const char *exts[] = { VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qci, 0, NULL, 2, exts };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    PFN_vkCreateIndirectCommandsLayoutEXT create_layout = (void *)vkGetDeviceProcAddr(dev, "vkCreateIndirectCommandsLayoutEXT");
    PFN_vkGetGeneratedCommandsMemoryRequirementsEXT mem_req = (void *)vkGetDeviceProcAddr(dev, "vkGetGeneratedCommandsMemoryRequirementsEXT");
    PFN_vkCmdExecuteGeneratedCommandsEXT execute = (void *)vkGetDeviceProcAddr(dev, "vkCmdExecuteGeneratedCommandsEXT");

    /* Color and depth targets, and readback. */
    VkImage images[2];
    VkImageView views[2];
    VkFormat formats[2] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT };
    VkImageUsageFlags usages[2] = { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT };
    VkImageAspectFlags aspects[2] = { VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT };
    for (int i = 0; i < 2; i++)
    {
        VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, formats[i],
                { W, H, 1 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usages[i] };
        CHECK(vkCreateImage(dev, &imci, NULL, &images[i]));
        VkMemoryRequirements ireq;
        vkGetImageMemoryRequirements(dev, images[i], &ireq);
        VkMemoryAllocateInfo ialloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, ireq.size,
                memory_type(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        VkDeviceMemory imem;
        CHECK(vkAllocateMemory(dev, &ialloc, NULL, &imem));
        CHECK(vkBindImageMemory(dev, images[i], imem, 0));
        VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, images[i], VK_IMAGE_VIEW_TYPE_2D,
                formats[i], {0}, { aspects[i], 0, 1, 0, 1 } };
        CHECK(vkCreateImageView(dev, &ivci, NULL, &views[i]));
    }
    struct buf readback = make_buffer(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    /* Objects: 0-3 are the quads, 4 the far background, 5 the curtain drawn after the executes. */
    struct buf objs = make_buffer(sizeof(struct obj) * 6, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    struct obj *o = objs.map;
    for (int i = 0; i < 4; i++)
        make_obj(&o[i], i);
    o[4] = (struct obj){ { -1, -1, 1, 1 }, { 0.5f, 0.5f, 0.5f, 1 }, 0.9f };
    o[5] = (struct obj){ { -1, -1, 1, 1 }, { 1, 0, 1, 1 }, 0.7f };
    VkDeviceAddress obj_va[6];
    for (int i = 0; i < 6; i++)
        obj_va[i] = objs.addr + i * sizeof(struct obj);

    /* CPU-bound index buffer: two padding indices, then 0..5 (drawn with firstIndex 2). */
    struct buf ib = make_buffer(64, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    uint16_t idx[] = { 9, 9, 0, 1, 2, 3, 4, 5 };
    memcpy(ib.map, idx, sizeof(idx));

    /* Pipeline: no vertex input, depth test less with writes. */
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 1, &pcr };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT,
          getenv("DIAG") ? shader(dgc_test2_diag_vs, sizeof(dgc_test2_diag_vs)) : shader(dgc_test2_vs, sizeof(dgc_test2_vs)), "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
          shader(dgc_test2_fs, sizeof(dgc_test2_fs)), "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, W, H, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, &vp, 1, &sc };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT };
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0,
            VK_TRUE, VK_TRUE, getenv("DEPTH_ALWAYS") ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_LESS };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0, 1, &cba };
    VkPipelineRenderingCreateInfo prci = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &formats[0],
            VK_FORMAT_D32_SFLOAT };
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &prci, 0, 2, stages, &vi, &ia,
            NULL, &vps, &rs, &ms, &ds, &cb, NULL, pl };
    VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));

    /* Layouts: A = push address @0 + draw @8 (stride 24); B = push address @0 + indexed draw @8 (stride 32). */
    VkIndirectCommandsPushConstantTokenEXT pc_token = { pcr };
    VkIndirectCommandsLayoutTokenEXT tokens_a[2] = {
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT, { .pPushConstant = &pc_token }, 0 },
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT, { NULL }, 8 },
    };
    VkIndirectCommandsLayoutTokenEXT tokens_b[2] = {
        tokens_a[0],
        { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, NULL, VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT, { NULL }, 8 },
    };
    VkIndirectCommandsLayoutCreateInfoEXT lci = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT, NULL, 0,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, SA, pl, 2, tokens_a };
    VkIndirectCommandsLayoutEXT layout_a, layout_b;
    CHECK(create_layout(dev, &lci, NULL, &layout_a));
    lci.indirectStride = 32;
    lci.pTokens = tokens_b;
    CHECK(create_layout(dev, &lci, NULL, &layout_b));

    struct buf stream_a = make_buffer(SA * 2, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    struct buf stream_b = make_buffer(32 * 2, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    for (int s = 0; s < 2; s++)
    {
        uint8_t *a = (uint8_t *)stream_a.map + SA * s, *b = (uint8_t *)stream_b.map + 32 * s;
        VkDrawIndirectCommand da = { 6, 1, getenv("FV0") ? 0 : 6 * s, 0 };
        VkDrawIndexedIndirectCommand db = { 6, 1, 2, 0, 0 };
        memcpy(a, &obj_va[getenv("SWAP") ? 1 - s : s], 8);
        memcpy(a + 8, &da, sizeof(da));
        memcpy(b, &obj_va[2 + s], 8);
        memcpy(b + 8, &db, sizeof(db));
    }

    VkGeneratedCommandsPipelineInfoEXT gpi = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT, NULL, pipeline };
    struct buf pre[2];
    VkDeviceSize pre_size[2];
    VkIndirectCommandsLayoutEXT layouts[2] = { layout_a, layout_b };
    for (int i = 0; i < 2; i++)
    {
        VkGeneratedCommandsMemoryRequirementsInfoEXT mri = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_EXT,
                &gpi, VK_NULL_HANDLE, layouts[i], 2, 0 };
        VkMemoryRequirements2 mr = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
        mem_req(dev, &mri, &mr);
        pre_size[i] = mr.memoryRequirements.size;
        pre[i] = make_buffer(pre_size[i], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    /* Record. */
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));

    VkImageMemoryBarrier2 to_att[2] = {
        { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, NULL, VK_PIPELINE_STAGE_2_NONE, 0,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, images[0], { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } },
        { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, NULL, VK_PIPELINE_STAGE_2_NONE, 0,
          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 0, 0, images[1], { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } },
    };
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, NULL, 0, 0, NULL, 0, NULL, 2, to_att };
    vkCmdPipelineBarrier2(cmd, &dep);

    VkRenderingAttachmentInfo color = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, views[0], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, views[1], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth = 1.0f;
    VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, { { 0, 0 }, { W, H } }, 1, 0, 1, &color, &depth };
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindIndexBuffer(cmd, ib.buf, 0, VK_INDEX_TYPE_UINT16);

    if (!getenv("ONLY_A"))
    {
        vkCmdPushConstants(cmd, pl, pcr.stageFlags, 0, 8, &obj_va[4]);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    if (getenv("PUSH_OBJ0"))
        vkCmdPushConstants(cmd, pl, pcr.stageFlags, 0, 8, &obj_va[0]);
    for (int k = 0; k < 2; k++)
    {
        int i = getenv("ORDER_BA") ? 1 - k : k;
        if (i == 1 && getenv("ONLY_A"))
            continue;
        struct buf *stream = i ? &stream_b : &stream_a;
        if (i == 0 && getenv("PLAIN_MULTI"))
        {
            vkCmdDrawIndirect(cmd, stream_a.buf, 8, 2, SA);
            continue;
        }
        if (i == 0 && getenv("PLAIN_INDIRECT"))
        {
            for (int s = 0; s < 2; s++)
            {
                vkCmdPushConstants(cmd, pl, pcr.stageFlags, 0, 8, &obj_va[s]);
                vkCmdDrawIndirect(cmd, stream_a.buf, SA * s + 8, 1, 0);
            }
            continue;
        }
        VkGeneratedCommandsInfoEXT gci = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_EXT, &gpi,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, layouts[i],
                stream->addr, (i ? 32 : SA) * 2, pre[i].addr, pre_size[i],
                (i == 0 && getenv("A_ONE")) ? 1 : 2, 0, 0 };
        execute(cmd, VK_FALSE, &gci);
    }

    if (!getenv("NO_CURTAIN"))
    {
        vkCmdPushConstants(cmd, pl, pcr.stageFlags, 0, 8, &obj_va[5]);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 to_src = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, NULL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0, images[0], { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &to_src;
    vkCmdPipelineBarrier2(cmd, &dep);
    VkBufferImageCopy copy = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { W, H, 1 } };
    vkCmdCopyImageToBuffer(cmd, images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buf, 1, &copy);
    CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

    if (getenv("DUMP_PRE"))
    {
        const uint32_t *d = pre[0].map;
        size_t n = pre_size[0] / 4;
        printf("A preprocess tail:");
        for (size_t k = n - 8; k < n; k++)
            printf(" %u", d[k]);
        printf("\n");
        for (size_t k = 0; k + 1 < n; k++)
            for (int s = 0; s < 4; s++)
                if (d[k] == (uint32_t)obj_va[s] && d[k + 1] == (uint32_t)(obj_va[s] >> 32))
                    printf("obj%d address at byte %zu\n", s, k * 4);
        printf("preprocess size %zu\n", n * 4);
        size_t rs = 2240 / 4;
        printf("root0 nonzero dwords:");
        for (size_t k = 0; k < rs; k++)
            if (d[k])
                printf(" [%zu]=%08x", k * 4, d[k]);
        printf("\nroot0 vs root1 diffs:");
        for (size_t k = 0; k < rs; k++)
            if (d[k] != d[rs + k])
                printf(" [%zu] %08x/%08x", k * 4, d[k], d[rs + k]);
        printf("\n");
    }

    /* Quads keep their color (they are in front of the curtain); everything else is the curtain. */
    const uint8_t *px = readback.map;
    const uint8_t magenta[4] = { 255, 0, 255, 255 };
    int failures = 0;
    for (int s = 0; s < 4; s++)
    {
        uint8_t want[4] = { colors[s][0] * 255, colors[s][1] * 255, colors[s][2] * 255, 255 };
        int good = 0, total = 0;
        for (int y = 2; y < 62; y++)
            for (int x = 16 * s + 2; x < 16 * s + 14; x++, total++)
                good += !memcmp(px + (y * W + x) * 4, want, 4);
        printf("%s draw %d: %d/%d pixels\n", s < 2 ? "A (non-indexed)" : "B (CPU index buffer)", s % 2, good, total);
        failures += good != total;
    }
    int curtain = 0, outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (x % 16 < 2 || x % 16 >= 14 || y < 2 || y >= 62)
            {
                outside++;
                curtain += !memcmp(px + (y * W + x) * 4, magenta, 4);
            }
    printf("curtain around the quads: %d/%d pixels\n", curtain, outside);
    if (getenv("DUMP_PX"))
        for (int y = 1; y < 64; y += 4)
        {
            for (int x = 1; x < 64; x += 2)
            {
                const uint8_t *p = px + (y * W + x) * 4;
                printf("%c", p[0] == 255 && p[1] == 255 ? 'Y' : p[0] == 255 && p[2] == 255 ? 'M' : p[0] == 255 ? 'R' : p[1] == 255 ? 'G' : p[2] == 255 ? 'B' : p[0] == 128 ? '.' : p[3] == 0 ? ' ' : '?');
            }
            printf("\n");
        }
    failures += curtain != outside;
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
