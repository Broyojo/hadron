/* The depth bounds test on a multisampled depth buffer, which the CTS does not cover. The first
 * draw stores a different depth in each of the four samples of every pixel (0.1, 0.35, 0.6,
 * 0.85); the second draws red with the bounds [0.3, 0.65], so only samples 1 and 2 may take it
 * and the resolved red is half (128). Three cases for the second draw: plain, writing depth (the
 * bounds test must see the depth from before the draw), and writing its own sample mask 0x5
 * (only sample 2 is left: 64); and plain again after the same draw made before the depth was
 * stored, while the depth is still the clear value 1, where it draws nothing. Runs on the first
 * Vulkan device; VK_DRIVER_FILES picks the driver.
 * tests/mac/vk-depth-bounds-msaa.sh writes the shaders, builds this and runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const uint32_t vs_spv[] =
#include "vk-depth-bounds-msaa.vert.inc"
;
static const uint32_t fs_depth_spv[] =
#include "vk-depth-bounds-msaa-depth.frag.inc"
;
static const uint32_t fs_red_spv[] =
#include "vk-depth-bounds-msaa-red.frag.inc"
;
static const uint32_t fs_red_mask_spv[] =
#include "vk-depth-bounds-msaa-red-mask.frag.inc"
;

#define SIZE 8
#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf( stderr, "%s:%d: %s failed: %d\n", __FILE__, __LINE__, #x, r_ ); exit( 2 ); } } while (0)

static VkDevice device;
static VkPhysicalDevice physical;
static VkPipelineCache cache;

static uint32_t memory_type( uint32_t bits, VkMemoryPropertyFlags flags )
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties( physical, &props );
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    fprintf( stderr, "no memory type\n" );
    exit( 2 );
}

static VkImage create_image( VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples,
                             VkImageView *view, VkImageAspectFlags aspect )
{
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = format, .extent = {SIZE, SIZE, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = samples, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
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

static VkPipeline pipeline( VkPipelineLayout layout, VkShaderModule vs, VkShaderModule fs,
                            VkBool32 sample_shading, VkBool32 depth_write, VkBool32 bounds )
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
        .rasterizationSamples = VK_SAMPLE_COUNT_4_BIT, .sampleShadingEnable = sample_shading,
        .minSampleShading = 1,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_write, .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS, .depthBoundsTestEnable = bounds,
        .minDepthBounds = 0.3f, .maxDepthBounds = 0.65f,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = sample_shading ? 0 : 0xf,
    };
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
    CHECK( vkCreateGraphicsPipelines( device, cache, 1, &info, NULL, &result ) );
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
    if (!features.depthBounds || !features.sampleRateShading)
    {
        printf( "skip: depthBounds %u, sampleRateShading %u\n", features.depthBounds,
                features.sampleRateShading );
        return 77;
    }

    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceFeatures enable = {.depthBounds = VK_TRUE, .sampleRateShading = VK_TRUE};
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

    VkImageView depth_view, color_view, resolve_view;
    VkImage depth = create_image( VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                  VK_SAMPLE_COUNT_4_BIT, &depth_view, VK_IMAGE_ASPECT_DEPTH_BIT );
    VkImage color = create_image( VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                  VK_SAMPLE_COUNT_4_BIT, &color_view, VK_IMAGE_ASPECT_COLOR_BIT );
    VkImage resolve = create_image( VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    VK_SAMPLE_COUNT_1_BIT, &resolve_view, VK_IMAGE_ASPECT_COLOR_BIT );

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

    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    CHECK( vkCreatePipelineLayout( device, &layout_info, NULL, &layout ) );
    VkShaderModule vs = shader( vs_spv, sizeof(vs_spv) );
    VkShaderModule fs_depth = shader( fs_depth_spv, sizeof(fs_depth_spv) );
    VkShaderModule red = shader( fs_red_spv, sizeof(fs_red_spv) );
    static const struct
    {
        const char *name;
        VkBool32 depth_write;
        int mask, draw_first;
        unsigned expect;
    } cases[] = {
        {"plain", VK_FALSE, 0, 0, 128},
        {"writing depth", VK_TRUE, 0, 0, 128},
        {"writing sample mask 0x5", VK_FALSE, 1, 0, 64},
        {"after a bounds draw before the depth", VK_FALSE, 0, 1, 128},
    };
    VkShaderModule red_mask = shader( fs_red_mask_spv, sizeof(fs_red_mask_spv) );

    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandPool pool;
    CHECK( vkCreateCommandPool( device, &pool_info, NULL, &pool ) );
    int failed = 0;

    /* The second round makes every pipeline again from a pipeline cache loaded from the
     * first round's data, the way a game's saved cache is loaded. */
    for (int round = 0; round < 2; round++)
    {
        VkPipelineCacheCreateInfo cache_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        void *cache_data = NULL;
        if (round)
        {
            size_t size = 0;
            CHECK( vkGetPipelineCacheData( device, cache, &size, NULL ) );
            cache_data = malloc( size );
            CHECK( vkGetPipelineCacheData( device, cache, &size, cache_data ) );
            vkDestroyPipelineCache( device, cache, NULL );
            cache_info.initialDataSize = size;
            cache_info.pInitialData = cache_data;
            printf( "from a pipeline cache of %zu bytes:\n", size );
        }
        CHECK( vkCreatePipelineCache( device, &cache_info, NULL, &cache ) );
        free( cache_data );
        VkPipeline fill = pipeline( layout, vs, fs_depth, VK_TRUE, VK_TRUE, VK_FALSE );

        for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            VkPipeline draw = pipeline( layout, vs, cases[c].mask ? red_mask : red, VK_FALSE,
                                        cases[c].depth_write, VK_TRUE );
            VkCommandBufferAllocateInfo cmd_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
            };
            VkCommandBuffer cmd;
            CHECK( vkAllocateCommandBuffers( device, &cmd_info, &cmd ) );
            VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            CHECK( vkBeginCommandBuffer( cmd, &begin ) );
            barrier( cmd, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL );
            barrier( cmd, color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            barrier( cmd, resolve, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            VkRenderingAttachmentInfo color_att = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = color_view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT, .resolveImageView = resolve_view,
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
            if (cases[c].draw_first)
            {
                vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw );
                vkCmdDraw( cmd, 3, 1, 0, 0 );
            }
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fill );
            vkCmdDraw( cmd, 3, 1, 0, 0 );
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw );
            vkCmdDraw( cmd, 3, 1, 0, 0 );
            vkCmdEndRendering( cmd );
            barrier( cmd, resolve, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
            VkBufferImageCopy region = {
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {SIZE, SIZE, 1},
            };
            vkCmdCopyImageToBuffer( cmd, resolve, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region );
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

            unsigned bad = 0, first = pixels[0];
            for (unsigned i = 0; i < SIZE * SIZE; i++)
                if (abs( (int)pixels[i * 4] - (int)cases[c].expect ) > 2) bad++;
            printf( "%s: red %u (expected %u), %u of %u pixels off\n", cases[c].name, first,
                    cases[c].expect, bad, SIZE * SIZE );
            if (bad) failed++;
            vkDestroyPipeline( device, draw, NULL );
        }
        vkDestroyPipeline( device, fill, NULL );
    }
    printf( "%s\n", failed ? "FAIL" : "PASS" );
    return failed ? 1 : 0;
}
