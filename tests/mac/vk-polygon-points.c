/* VK_POLYGON_MODE_POINT with culling, from a vertex shader and from a geometry shader. A strip
 * of two triangles, (-0.5,-0.5) (-0.5,0.5) (0.5,-0.5) (0.5,0.5), both counter-clockwise on
 * screen (the second, odd, one taken in the order 2 1 3, as Vulkan takes it), with the front
 * face counter-clockwise and points of size 3. With back faces culled all four corners are drawn;
 * with front faces culled none is. A geometry shader that took the odd triangle in the order
 * 1 2 3 would see it as back-facing, and the corner only it has (0.5,0.5) would be missing. The
 * CTS culling tests draw from a vertex shader only. Runs on the first Vulkan device;
 * VK_DRIVER_FILES picks the driver. tests/mac/vk-polygon-points.sh writes the shaders, builds
 * this and runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const uint32_t vs_strip_spv[] =
#include "vk-polygon-points-strip.vert.inc"
;
static const uint32_t vs_point_spv[] =
#include "vk-polygon-points-point.vert.inc"
;
static const uint32_t gs_spv[] =
#include "vk-polygon-points.geom.inc"
;
static const uint32_t fs_spv[] =
#include "vk-polygon-points.frag.inc"
;

#define SIZE 32
#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf( stderr, "%s:%d: %s failed: %d\n", __FILE__, __LINE__, #x, r_ ); exit( 2 ); } } while (0)

static VkDevice device;
static VkPhysicalDevice physical;

static uint32_t memory_type( uint32_t bits, VkMemoryPropertyFlags flags )
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties( physical, &props );
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    fprintf( stderr, "no memory type\n" );
    exit( 2 );
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

static VkPipeline pipeline( VkPipelineLayout layout, VkShaderModule vs, VkShaderModule gs,
                            VkShaderModule fs, VkCullModeFlags cull )
{
    VkPipelineShaderStageCreateInfo stages[3] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_GEOMETRY_BIT, .module = gs, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = gs ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    };
    VkViewport viewport = {0, 0, SIZE, SIZE, 0, 1};
    VkRect2D scissor = {{0, 0}, {SIZE, SIZE}};
    VkPipelineViewportStateCreateInfo viewports = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_POINT, .cullMode = cull,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xf};
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment,
    };
    VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering,
        .stageCount = gs ? 3 : 2, .pStages = stages, .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly, .pViewportState = &viewports,
        .pRasterizationState = &raster, .pMultisampleState = &ms, .pColorBlendState = &blend,
        .layout = layout,
    };
    VkPipeline result;
    CHECK( vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &info, NULL, &result ) );
    return result;
}

static void barrier( VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to )
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = from, .newLayout = to, .image = image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
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
    if (!features.fillModeNonSolid || !features.geometryShader || !features.shaderTessellationAndGeometryPointSize)
    {
        printf( "skip: fillModeNonSolid %u, geometryShader %u, shaderTessellationAndGeometryPointSize %u\n",
                features.fillModeNonSolid, features.geometryShader,
                features.shaderTessellationAndGeometryPointSize );
        return 77;
    }

    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceFeatures enable = {
        .fillModeNonSolid = VK_TRUE, .geometryShader = VK_TRUE,
        .shaderTessellationAndGeometryPointSize = VK_TRUE,
    };
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
    free( exts );
    CHECK( vkCreateDevice( physical, &device_info, NULL, &device ) );
    VkQueue queue;
    vkGetDeviceQueue( device, 0, 0, &queue );

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {SIZE, SIZE, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    };
    VkImage image;
    VkMemoryRequirements req;
    VkDeviceMemory image_memory;
    CHECK( vkCreateImage( device, &image_info, NULL, &image ) );
    vkGetImageMemoryRequirements( device, image, &req );
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
        .memoryTypeIndex = memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ),
    };
    CHECK( vkAllocateMemory( device, &alloc, NULL, &image_memory ) );
    CHECK( vkBindImageMemory( device, image, image_memory, 0 ) );
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView view;
    CHECK( vkCreateImageView( device, &view_info, NULL, &view ) );

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = SIZE * SIZE * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    CHECK( vkCreateBuffer( device, &buffer_info, NULL, &buffer ) );
    vkGetBufferMemoryRequirements( device, buffer, &req );
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    CHECK( vkAllocateMemory( device, &alloc, NULL, &buffer_memory ) );
    CHECK( vkBindBufferMemory( device, buffer, buffer_memory, 0 ) );
    unsigned char *pixels;
    CHECK( vkMapMemory( device, buffer_memory, 0, VK_WHOLE_SIZE, 0, (void **)&pixels ) );

    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    CHECK( vkCreatePipelineLayout( device, &layout_info, NULL, &layout ) );
    VkShaderModule vs_strip = shader( vs_strip_spv, sizeof(vs_strip_spv) );
    VkShaderModule vs_point = shader( vs_point_spv, sizeof(vs_point_spv) );
    VkShaderModule gs = shader( gs_spv, sizeof(gs_spv) );
    VkShaderModule fs = shader( fs_spv, sizeof(fs_spv) );

    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandPool pool;
    CHECK( vkCreateCommandPool( device, &pool_info, NULL, &pool ) );

    /* The corners in pixels: NDC -0.5 and 0.5 on a 32-pixel target. */
    static const int corners[4][2] = {{8, 8}, {8, 24}, {24, 8}, {24, 24}};
    int failed = 0;

    for (int with_gs = 0; with_gs < 2; with_gs++)
    {
        for (int cull_front = 0; cull_front < 2; cull_front++)
        {
            VkPipeline draw = pipeline( layout, with_gs ? vs_point : vs_strip, with_gs ? gs : VK_NULL_HANDLE,
                                        fs, cull_front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
            VkCommandBufferAllocateInfo cmd_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
            };
            VkCommandBuffer cmd;
            CHECK( vkAllocateCommandBuffers( device, &cmd_info, &cmd ) );
            VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            CHECK( vkBeginCommandBuffer( cmd, &begin ) );
            barrier( cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            VkRenderingAttachmentInfo color_att = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            };
            VkRenderingInfo rendering = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {SIZE, SIZE}},
                .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_att,
            };
            vkCmdBeginRendering( cmd, &rendering );
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw );
            vkCmdDraw( cmd, with_gs ? 1 : 4, 1, 0, 0 );
            vkCmdEndRendering( cmd );
            barrier( cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
            VkBufferImageCopy region = {
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {SIZE, SIZE, 1},
            };
            vkCmdCopyImageToBuffer( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region );
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

            /* Each corner's 3x3 point covers the pixels around it, nothing else is red. */
            unsigned lit = 0, red = 0;
            char drawn[5] = "....";
            for (int c = 0; c < 4; c++)
                if (pixels[(corners[c][1] * SIZE + corners[c][0]) * 4] > 128) { lit++; drawn[c] = 'x'; }
            for (unsigned i = 0; i < SIZE * SIZE; i++)
                if (pixels[i * 4] > 128) red++;
            unsigned expect = cull_front ? 0 : 4;
            int ok = lit == expect && red == expect * 9;
            printf( "%s, %s culled: corners drawn %s (%u of 4, expected %u), %u red pixels (expected %u)%s\n",
                    with_gs ? "geometry shader" : "vertex shader", cull_front ? "front" : "back", drawn,
                    lit, expect, red, expect * 9, ok ? "" : "  <- wrong" );
            if (!ok) failed++;
            vkDestroyPipeline( device, draw, NULL );
        }
    }
    printf( "%s\n", failed ? "FAIL" : "PASS" );
    return failed ? 1 : 0;
}
