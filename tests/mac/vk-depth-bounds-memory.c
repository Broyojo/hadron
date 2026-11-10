/* Memory a command buffer takes for draws that write depth with the depth bounds test on. Where
 * Metal has no depth bounds test (before Apple GPU family 10) KosmicKrisp emulates it, and a draw
 * that writes depth reads a copy of the depth attachment, kept until the command buffer is reset.
 * Bounds of [0, 1] cannot reject anything when depth is limited to [0, 1], and DOOM (2016) draws
 * that way thousands of times a frame (issue #128): it grew to 30 GB. This records DRAWS such
 * draws on a SIZE x SIZE depth buffer, at depth 0.5, and fails if the process grew by more than a
 * tenth of what a copy per draw takes. Then it checks the test still works after them: a red draw
 * with bounds [0.4, 0.6] must cover everything, a green one with [0.6, 0.9] nothing. Runs on the
 * first Vulkan device; VK_DRIVER_FILES picks the driver.
 * tests/mac/vk-depth-bounds-memory.sh writes the shaders, builds this and runs it. */
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const uint32_t vs_spv[] =
#include "vk-depth-bounds-memory.vert.inc"
;
static const uint32_t fs_spv[] =
#include "vk-depth-bounds-memory.frag.inc"
;

#define SIZE 1024
#define DRAWS 128
#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf( stderr, "%s:%d: %s failed: %d\n", __FILE__, __LINE__, #x, r_ ); exit( 2 ); } } while (0)

static VkDevice device;
static VkPhysicalDevice physical;

static uint64_t footprint( void )
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info( mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count ) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
}

static uint32_t memory_type( uint32_t bits, VkMemoryPropertyFlags flags )
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties( physical, &props );
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    fprintf( stderr, "no memory type\n" );
    exit( 2 );
}

static VkImage create_image( VkFormat format, VkImageUsageFlags usage, VkImageView *view,
                             VkImageAspectFlags aspect )
{
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = format, .extent = {SIZE, SIZE, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
    };
    VkMemoryRequirements req;
    VkDeviceMemory memory;
    VkImage image;

    CHECK( vkCreateImage( device, &info, NULL, &image ) );
    vkGetImageMemoryRequirements( device, image, &req );
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
        .memoryTypeIndex = memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ),
    };
    CHECK( vkAllocateMemory( device, &alloc, NULL, &memory ) );
    CHECK( vkBindImageMemory( device, image, memory, 0 ) );
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    CHECK( vkCreateImageView( device, &view_info, NULL, view ) );
    return image;
}

static VkShaderModule shader( const uint32_t *code, size_t size )
{
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code,
    };
    VkShaderModule module;
    CHECK( vkCreateShaderModule( device, &info, NULL, &module ) );
    return module;
}

/* The color comes from a push constant; depth_write draws at 0.5 with the test ALWAYS. */
static VkPipeline pipeline( VkPipelineLayout layout, VkShaderModule vs, VkShaderModule fs,
                            VkBool32 depth_write, float min_bound, float max_bound )
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {0, 0, SIZE, SIZE, 0, 1};
    VkRect2D scissor = {{0, 0}, {SIZE, SIZE}};
    VkPipelineViewportStateCreateInfo viewports = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS, .depthBoundsTestEnable = VK_TRUE,
        .minDepthBounds = min_bound, .maxDepthBounds = max_bound,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xf};
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment,
    };
    VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format, .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
    };
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly, .pViewportState = &viewports,
        .pRasterizationState = &raster, .pMultisampleState = &ms, .pDepthStencilState = &ds,
        .pColorBlendState = &blend, .layout = layout,
    };
    VkPipeline result;
    CHECK( vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &info, NULL, &result ) );
    return result;
}

static void barrier( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to )
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = from, .newLayout = to, .image = image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          0, 0, NULL, 0, NULL, 1, &b );
}

static void draw( VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline pipe, float r, float g )
{
    float color[4] = {r, g, 0, 1};
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe );
    vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(color), color );
    vkCmdDraw( cmd, 3, 1, 0, 0 );
}

int main( void )
{
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    const char *instance_ext[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = instance_ext,
    };
    VkInstance instance;
    uint32_t count = 1;

    CHECK( vkCreateInstance( &instance_info, NULL, &instance ) );
    vkEnumeratePhysicalDevices( instance, &count, &physical );
    if (!count) { fprintf( stderr, "no device\n" ); return 2; }

    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties( physical, &props );
    vkGetPhysicalDeviceFeatures( physical, &features );
    printf( "device: %s\n", props.deviceName );
    if (!features.depthBounds)
    {
        printf( "skip: no depthBounds\n" );
        return 77;
    }

    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceFeatures enable = {.depthBounds = VK_TRUE};
    const char *device_ext[] = {"VK_KHR_portability_subset"};
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &features13, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info, .pEnabledFeatures = &enable,
    };
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties( physical, NULL, &ext_count, NULL );
    VkExtensionProperties *exts = calloc( ext_count, sizeof(*exts) );
    vkEnumerateDeviceExtensionProperties( physical, NULL, &ext_count, exts );
    for (uint32_t i = 0; i < ext_count; i++)
        if (!strcmp( exts[i].extensionName, device_ext[0] ))
        {
            device_info.enabledExtensionCount = 1;
            device_info.ppEnabledExtensionNames = device_ext;
        }
    CHECK( vkCreateDevice( physical, &device_info, NULL, &device ) );
    VkQueue queue;
    vkGetDeviceQueue( device, 0, 0, &queue );

    VkImageView depth_view, color_view;
    VkImage depth = create_image( VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                  &depth_view, VK_IMAGE_ASPECT_DEPTH_BIT );
    VkImage color = create_image( VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                  &color_view, VK_IMAGE_ASPECT_COLOR_BIT );

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = SIZE * SIZE * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VkBuffer buffer;
    VkMemoryRequirements req;
    VkDeviceMemory buffer_memory;
    CHECK( vkCreateBuffer( device, &buffer_info, NULL, &buffer ) );
    vkGetBufferMemoryRequirements( device, buffer, &req );
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
        .memoryTypeIndex = memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ),
    };
    CHECK( vkAllocateMemory( device, &alloc, NULL, &buffer_memory ) );
    CHECK( vkBindBufferMemory( device, buffer, buffer_memory, 0 ) );
    unsigned char *pixels;
    CHECK( vkMapMemory( device, buffer_memory, 0, VK_WHOLE_SIZE, 0, (void **)&pixels ) );

    VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    };
    VkPipelineLayout layout;
    CHECK( vkCreatePipelineLayout( device, &layout_info, NULL, &layout ) );
    VkShaderModule vs = shader( vs_spv, sizeof(vs_spv) );
    VkShaderModule fs = shader( fs_spv, sizeof(fs_spv) );
    VkPipeline full = pipeline( layout, vs, fs, VK_TRUE, 0.0f, 1.0f );
    VkPipeline inside = pipeline( layout, vs, fs, VK_FALSE, 0.4f, 0.6f );
    VkPipeline outside = pipeline( layout, vs, fs, VK_FALSE, 0.6f, 0.9f );

    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandPool pool;
    CHECK( vkCreateCommandPool( device, &pool_info, NULL, &pool ) );
    VkCommandBufferAllocateInfo cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    CHECK( vkAllocateCommandBuffers( device, &cmd_info, &cmd ) );

    uint64_t before = footprint();
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK( vkBeginCommandBuffer( cmd, &begin ) );
    barrier( cmd, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL );
    barrier( cmd, color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
    VkRenderingAttachmentInfo color_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = color_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    VkRenderingAttachmentInfo depth_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.depthStencil.depth = 1,
    };
    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {SIZE, SIZE}},
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_att,
        .pDepthAttachment = &depth_att,
    };
    vkCmdBeginRendering( cmd, &rendering );
    for (int i = 0; i < DRAWS; i++) draw( cmd, layout, full, 0, 0 );
    draw( cmd, layout, inside, 1, 0 );
    draw( cmd, layout, outside, 0, 1 );
    vkCmdEndRendering( cmd );
    barrier( cmd, color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {SIZE, SIZE, 1},
    };
    vkCmdCopyImageToBuffer( cmd, color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region );
    VkMemoryBarrier host = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                          1, &host, 0, NULL, 0, NULL );
    CHECK( vkEndCommandBuffer( cmd ) );
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd,
    };
    memset( pixels, 0xee, SIZE * SIZE * 4 );
    CHECK( vkQueueSubmit( queue, 1, &submit, VK_NULL_HANDLE ) );
    CHECK( vkQueueWaitIdle( queue ) );
    uint64_t grown = footprint() - before;

    /* A copy of the depth attachment per draw would take DRAWS * SIZE * SIZE * 4 bytes. */
    uint64_t per_draw_copies = (uint64_t)DRAWS * SIZE * SIZE * 4;
    int failed = 0;
    printf( "%d draws writing depth with bounds [0, 1]: the process grew by %llu MB "
            "(a copy per draw would be %llu MB)\n", DRAWS, grown >> 20, per_draw_copies >> 20 );
    if (grown > per_draw_copies / 10) failed++;

    unsigned bad = 0;
    for (unsigned i = 0; i < SIZE * SIZE; i++)
        if (pixels[i * 4] < 250 || pixels[i * 4 + 1] > 5) bad++;
    printf( "bounds [0.4, 0.6] drew, [0.6, 0.9] did not: %u of %u pixels off (first: red %u, green %u)\n",
            bad, SIZE * SIZE, pixels[0], pixels[1] );
    if (bad) failed++;

    printf( "%s\n", failed ? "FAIL" : "PASS" );
    return failed ? 1 : 0;
}
