/* vkprobe: list the Vulkan devices a Windows program sees through Wine's vulkan-1.dll.
 *
 *   toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -O2 -I/opt/homebrew/include \
 *       -o build/tools/vkprobe.exe tools/vkprobe.c
 *   scripts/wine-run build/tools/vkprobe.exe
 */
#define VK_NO_PROTOTYPES
#include <stdio.h>
#include <windows.h>
#include <vulkan/vulkan.h>

int main(void)
{
    HMODULE vulkan = LoadLibraryA("vulkan-1.dll");
    if (!vulkan) { printf("vulkan-1.dll: not found (%lu)\n", GetLastError()); return 1; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(vulkan, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkprobe", 1, NULL, 0, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL };
    VkInstance instance;
    VkResult result = create_instance(&info, NULL, &instance);
    if (result != VK_SUCCESS) { printf("vkCreateInstance: %d\n", result); return 1; }

    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties2 properties2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2");
    PFN_vkEnumerateDeviceExtensionProperties extensions = (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties");

    VkPhysicalDevice devices[8];
    uint32_t count = 8;
    enumerate(instance, &count, devices);
    printf("devices: %u\n", count);
    for (uint32_t i = 0; i < count; i++)
    {
        VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver };
        properties2(devices[i], &props);
        uint32_t extension_count = 0;
        extensions(devices[i], NULL, &extension_count, NULL);
        printf("  %s: Vulkan %u.%u.%u, %s %s, %u device extensions\n", props.properties.deviceName,
               VK_API_VERSION_MAJOR(props.properties.apiVersion), VK_API_VERSION_MINOR(props.properties.apiVersion),
               VK_API_VERSION_PATCH(props.properties.apiVersion), driver.driverName, driver.driverInfo, extension_count);
    }
    return 0;
}
