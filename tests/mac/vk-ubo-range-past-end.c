/* A uniform buffer descriptor whose range runs past the end of its buffer, as DOOM (2016) writes
 * them (issue #128: a 64 KB range on buffers of 49152 and 65520 bytes). That is invalid
 * (VUID-VkDescriptorBufferInfo-range-00342), so Vulkan says nothing about the result, but a
 * driver should not abort or read other memory. Buffer A is the small one; buffer B, filled with
 * a marker, sits right after it in the same allocation. A compute shader reads A's descriptor
 * inside A and past its end, with robustBufferAccess2 on, through a plain and a dynamic uniform
 * buffer descriptor. Inside it must see A's data; past the end it must not see B's marker (a
 * driver that keeps the range reads B there) and, with the range clamped to the buffer, sees
 * zero. Runs on the first Vulkan device; VK_DRIVER_FILES picks the driver.
 * tests/mac/vk-ubo-range-past-end.sh writes the shader, builds this and runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const uint32_t cs_spv[] =
#include "vk-ubo-range-past-end.comp.inc"
;

#define RANGE 65536
#define MARKER 0xdeadbeefu
#define READS 4
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

static VkBuffer create_buffer( VkDeviceSize size, VkBufferUsageFlags usage )
{
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage,
    };
    VkBuffer buffer;
    CHECK( vkCreateBuffer( device, &info, NULL, &buffer ) );
    return buffer;
}

/* Returns 0 if every read is as expected. */
static int run( VkQueue queue, uint32_t family, VkDeviceSize a_size, VkBool32 dynamic )
{
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkBuffer a = create_buffer( a_size, usage ), b = create_buffer( RANGE, usage );
    VkBuffer out = create_buffer( READS * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    VkMemoryRequirements a_req, b_req, out_req;
    vkGetBufferMemoryRequirements( device, a, &a_req );
    vkGetBufferMemoryRequirements( device, b, &b_req );
    vkGetBufferMemoryRequirements( device, out, &out_req );
    VkDeviceSize b_offset = (a_req.size + b_req.alignment - 1) & ~(b_req.alignment - 1);
    VkDeviceSize out_offset = (b_offset + b_req.size + out_req.alignment - 1) & ~(out_req.alignment - 1);
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = out_offset + out_req.size,
        .memoryTypeIndex = memory_type( a_req.memoryTypeBits & b_req.memoryTypeBits & out_req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ),
    };
    VkDeviceMemory memory;
    CHECK( vkAllocateMemory( device, &alloc, NULL, &memory ) );
    CHECK( vkBindBufferMemory( device, a, memory, 0 ) );
    CHECK( vkBindBufferMemory( device, b, memory, b_offset ) );
    CHECK( vkBindBufferMemory( device, out, memory, out_offset ) );

    uint8_t *map;
    CHECK( vkMapMemory( device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&map ) );
    memset( map, 0, alloc.allocationSize );
    for (VkDeviceSize i = 0; i < a_size / 4; i++) ((uint32_t *)map)[i] = (uint32_t)i + 1;
    for (VkDeviceSize i = 0; i < RANGE / 4; i++) ((uint32_t *)(map + b_offset))[i] = MARKER;

    VkDescriptorType ubo_type = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                        : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, ubo_type, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
    };
    VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings,
    };
    VkDescriptorSetLayout set_layout;
    CHECK( vkCreateDescriptorSetLayout( device, &set_layout_info, NULL, &set_layout ) );
    VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, READS * 4};
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
        .pSetLayouts = &set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    };
    VkPipelineLayout layout;
    CHECK( vkCreatePipelineLayout( device, &layout_info, NULL, &layout ) );

    VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(cs_spv), .pCode = cs_spv,
    };
    VkShaderModule module;
    CHECK( vkCreateShaderModule( device, &module_info, NULL, &module ) );
    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"},
        .layout = layout,
    };
    VkPipeline pipeline;
    CHECK( vkCreateComputePipelines( device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline ) );

    VkDescriptorPoolSize sizes[2] = {{ubo_type, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
        .poolSizeCount = 2, .pPoolSizes = sizes,
    };
    VkDescriptorPool pool;
    CHECK( vkCreateDescriptorPool( device, &pool_info, NULL, &pool ) );
    VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool,
        .descriptorSetCount = 1, .pSetLayouts = &set_layout,
    };
    VkDescriptorSet set;
    CHECK( vkAllocateDescriptorSets( device, &set_info, &set ) );
    VkDescriptorBufferInfo ubo = {a, 0, RANGE}, ssbo = {out, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
         .descriptorCount = 1, .descriptorType = ubo_type, .pBufferInfo = &ubo},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &ssbo},
    };
    vkUpdateDescriptorSets( device, 2, writes, 0, NULL );

    /* The vec4 indices read: A's first and last, the first past A's end, and the last in range. */
    uint32_t index[READS] = {0, (uint32_t)(a_size / 16) - 1, (uint32_t)(a_size / 16), RANGE / 16 - 1};

    VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family,
    };
    VkCommandPool cmd_pool;
    CHECK( vkCreateCommandPool( device, &cmd_pool_info, NULL, &cmd_pool ) );
    VkCommandBufferAllocateInfo cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    CHECK( vkAllocateCommandBuffers( device, &cmd_info, &cmd ) );
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK( vkBeginCommandBuffer( cmd, &begin ) );
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline );
    uint32_t dynamic_offset = 0;
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set,
                             dynamic ? 1 : 0, &dynamic_offset );
    vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(index), index );
    vkCmdDispatch( cmd, 1, 1, 1 );
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                          1, &barrier, 0, NULL, 0, NULL );
    CHECK( vkEndCommandBuffer( cmd ) );
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd,
    };
    CHECK( vkQueueSubmit( queue, 1, &submit, VK_NULL_HANDLE ) );
    CHECK( vkQueueWaitIdle( queue ) );

    const uint32_t *result = (const uint32_t *)(map + out_offset);
    int failed = 0;
    for (int i = 0; i < READS; i++) {
        uint32_t expect = index[i] < a_size / 16 ? index[i] * 4 + 1 : 0;
        printf( "  %s size %llu: vec4 %u reads %#x (expected %#x)\n", dynamic ? "dynamic" : "plain",
                (unsigned long long)a_size, index[i], result[i * 4], expect );
        if (result[i * 4] != expect) failed = 1;
    }

    vkDestroyCommandPool( device, cmd_pool, NULL );
    vkDestroyDescriptorPool( device, pool, NULL );
    vkDestroyPipeline( device, pipeline, NULL );
    vkDestroyShaderModule( device, module, NULL );
    vkDestroyPipelineLayout( device, layout, NULL );
    vkDestroyDescriptorSetLayout( device, set_layout, NULL );
    vkDestroyBuffer( device, a, NULL );
    vkDestroyBuffer( device, b, NULL );
    vkDestroyBuffer( device, out, NULL );
    vkFreeMemory( device, memory, NULL );
    return failed;
}

int main( void )
{
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = (const char *[]){VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME},
    };
    VkInstance instance;
    CHECK( vkCreateInstance( &instance_info, NULL, &instance ) );
    uint32_t count = 1;
    vkEnumeratePhysicalDevices( instance, &count, &physical );
    if (!count) { fprintf( stderr, "no Vulkan device\n" ); return 2; }

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &robustness2,
    };
    vkGetPhysicalDeviceFeatures2( physical, &features );
    if (!features.features.robustBufferAccess || !robustness2.robustBufferAccess2) {
        printf( "robustBufferAccess2 not supported\n" );
        return 77;
    }

    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties( physical, NULL, &ext_count, NULL );
    VkExtensionProperties *exts = calloc( ext_count, sizeof(*exts) );
    vkEnumerateDeviceExtensionProperties( physical, NULL, &ext_count, exts );
    const char *names[2] = {VK_EXT_ROBUSTNESS_2_EXTENSION_NAME};
    uint32_t name_count = 1;
    for (uint32_t i = 0; i < ext_count; i++)
        if (!strcmp( exts[i].extensionName, "VK_KHR_portability_subset" ))
            names[name_count++] = "VK_KHR_portability_subset";
    free( exts );

    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0,
        .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceRobustness2FeaturesEXT enable_robustness2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
        .robustBufferAccess2 = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 enable = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable_robustness2,
        .features = {.robustBufferAccess = VK_TRUE},
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enable,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = name_count, .ppEnabledExtensionNames = names,
    };
    CHECK( vkCreateDevice( physical, &device_info, NULL, &device ) );
    VkQueue queue;
    vkGetDeviceQueue( device, 0, 0, &queue );

    int failed = 0;
    VkDeviceSize sizes[2] = {49152, 65520};
    for (int s = 0; s < 2; s++)
        for (int dynamic = 0; dynamic < 2; dynamic++)
            failed |= run( queue, 0, sizes[s], dynamic );

    vkDestroyDevice( device, NULL );
    vkDestroyInstance( instance, NULL );
    printf( "%s\n", failed ? "FAIL" : "PASS" );
    return failed;
}
