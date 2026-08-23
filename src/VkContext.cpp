#define VMA_IMPLEMENTATION // The vk_mem_alloc.h included in VkContext.h will compile the bodies of the VMA functions (STB-style implementation)
#include "VkContext.h"

// Singleton containing the Vulkan context.
static GraphicsContext g_ctx;
GraphicsContext& ctx() {
    return g_ctx;
}

#ifdef OM3D_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*
) {
    if(severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }

    const bool is_error = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    (is_error ? std::cerr : std::cout) << (is_error ? "[VK][ERROR] " : "[VK] ")
                                       << data->pMessage << std::endl;

    if(is_error) {
        break_in_debugger();
    }
    return VK_FALSE;
}

static bool has_instance_layer(const char* name) {
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for(const VkLayerProperties& layer : layers) {
        if(std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

static VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info() {
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };
}
#endif

static bool has_device_extension(VkPhysicalDevice physical, const char* name) {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, exts.data());
    for(const VkExtensionProperties& ext : exts) {
        if(std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool find_graphics_present_queue(VkPhysicalDevice physical, VkSurfaceKHR surface, u32* out_family) {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());

    for(u32 i = 0; i != count; ++i) {
        if(!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
        if(present) {
            *out_family = i;
            return true;
        }
    }
    return false;
}

static int rate_device(VkPhysicalDevice physical, VkSurfaceKHR surface, u32* out_family) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    if(props.apiVersion < VK_API_VERSION_1_3) {
        return 0;
    }

    VkPhysicalDeviceVulkan13Features vk13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk13,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features);

    if(!vk13.dynamicRendering || !vk13.synchronization2 || !features.features.samplerAnisotropy) {
        return 0;
    }

    if(!has_device_extension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return 0;
    }

    if(!find_graphics_present_queue(physical, surface, out_family)) {
        return 0;
    }

    int score = 1;
    if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 100;
    }
    return score;
}

void vk_init(GLFWwindow* window) {
    ALWAYS_ASSERT(window, "init_graphics requires a GLFW window");
    g_ctx.window = window;

    vk_check(volkInitialize());

    u32 glfw_ext_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    ALWAYS_ASSERT(glfw_exts && glfw_ext_count, "GLFW was not built with Vulkan support");

    std::vector<const char*> instance_exts(glfw_exts, glfw_exts + glfw_ext_count);

#ifdef OM3D_DEBUG
    const bool use_validation = has_instance_layer("VK_LAYER_KHRONOS_validation");
    if(!use_validation) {
        std::cerr << "VK_LAYER_KHRONOS_validation not found; continuing without it" << std::endl;
    }
    instance_exts.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    const VkDebugUtilsMessengerCreateInfoEXT debug_ci = debug_messenger_info();
#endif

    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "OM3D",
        .apiVersion = VK_API_VERSION_1_3,
    };

    const VkInstanceCreateInfo instance_ci{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef OM3D_DEBUG
        .pNext = &debug_ci,
#endif
        .pApplicationInfo = &app_info,
#ifdef OM3D_DEBUG
        .enabledLayerCount = use_validation ? 1u : 0u,
        .ppEnabledLayerNames = use_validation ? &validation_layer : nullptr,
#endif
        .enabledExtensionCount = u32(instance_exts.size()),
        .ppEnabledExtensionNames = instance_exts.data(),
    };
    vk_check(vkCreateInstance(&instance_ci, nullptr, &g_ctx.instance));
    volkLoadInstance(g_ctx.instance);

#ifdef OM3D_DEBUG
    vk_check(vkCreateDebugUtilsMessengerEXT(g_ctx.instance, &debug_ci, nullptr, &g_ctx.debug_messenger));
#endif

    vk_check(glfwCreateWindowSurface(g_ctx.instance, window, nullptr, &g_ctx.surface));

    u32 device_count = 0;
    vk_check(vkEnumeratePhysicalDevices(g_ctx.instance, &device_count, nullptr));
    ALWAYS_ASSERT(device_count, "No Vulkan-capable GPUs found");
    std::vector<VkPhysicalDevice> devices(device_count);
    vk_check(vkEnumeratePhysicalDevices(g_ctx.instance, &device_count, devices.data()));

    int best_score = 0;
    for(VkPhysicalDevice physical : devices) {
        u32 family = 0;
        const int score = rate_device(physical, g_ctx.surface, &family);
        if(score > best_score) {
            best_score = score;
            g_ctx.physical_device = physical;
            g_ctx.graphics_queue_family = family;
        }
    }
    ALWAYS_ASSERT(g_ctx.physical_device, "No suitable Vulkan 1.3 GPU with graphics+present and swapchain");

    VkPhysicalDeviceProperties2 props{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    vkGetPhysicalDeviceProperties2(g_ctx.physical_device, &props);
    g_ctx.device_name = props.properties.deviceName;
    std::cout << "Vulkan 1.3 initialized on " << g_ctx.device_name << std::endl;

    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_ctx.graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char* device_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceVulkan13Features vk13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const VkPhysicalDeviceFeatures vk10_features{
        .samplerAnisotropy = VK_TRUE,
    };

    const VkDeviceCreateInfo device_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk13_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_ci,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_exts,
        .pEnabledFeatures = &vk10_features,
    };
    vk_check(vkCreateDevice(g_ctx.physical_device, &device_ci, nullptr, &g_ctx.device));
    vkGetDeviceQueue(g_ctx.device, g_ctx.graphics_queue_family, 0, &g_ctx.graphics_queue);
    volkLoadDevice(g_ctx.device);

    const VmaVulkanFunctions vma_functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    VmaAllocatorCreateInfo allocator_ci{
        .physicalDevice = g_ctx.physical_device,
        .device = g_ctx.device,
        .pVulkanFunctions = &vma_functions,
        .instance = g_ctx.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    vk_check(vmaCreateAllocator(&allocator_ci, &g_ctx.allocator));
}

void vk_destroy() {
    if(g_ctx.device) {
        vkDeviceWaitIdle(g_ctx.device);
    }
    if(g_ctx.allocator) {
        vmaDestroyAllocator(g_ctx.allocator);
        g_ctx.allocator = VK_NULL_HANDLE;
    }
    if(g_ctx.device) {
        vkDestroyDevice(g_ctx.device, nullptr);
        g_ctx.device = VK_NULL_HANDLE;
    }
    if(g_ctx.surface) {
        vkDestroySurfaceKHR(g_ctx.instance, g_ctx.surface, nullptr);
        g_ctx.surface = VK_NULL_HANDLE;
    }
#ifdef OM3D_DEBUG
    if(g_ctx.debug_messenger) {
        vkDestroyDebugUtilsMessengerEXT(g_ctx.instance, g_ctx.debug_messenger, nullptr);
        g_ctx.debug_messenger = VK_NULL_HANDLE;
    }
#endif
    if(g_ctx.instance) {
        vkDestroyInstance(g_ctx.instance, nullptr);
        g_ctx.instance = VK_NULL_HANDLE;
    }
    g_ctx = {};
}