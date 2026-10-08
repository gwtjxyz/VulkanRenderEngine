module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

#ifdef DISABLE_IMPORT_STD
#include <iostream>
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vk_mem_alloc.h"

export module vulkan_instance;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifndef DISABLE_IMPORT_STD
import std;
#endif

// CLion hack to fix ambiguous symbol errors
// This doesn't actually do anything, but convinces CLion that there are no ambiguous symbols in the code
// Otherwise the code compiles and runs with no issues, but the IDE complains regardless
#ifdef __JETBRAINS_IDE__
class DispatchLoaderHack {
public:
    static void init() {}
    static void init(const vk::Instance & instance) { (void)instance; }
};

DispatchLoaderHack dispatchLoaderHack;
#endif

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
    void * pUserData
) {
    std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

    return vk::False;
}

// Holds Vulkan objects like instance, device, etc.
export class VulkanInstance {
public:
    VulkanInstance() = default;

    ~VulkanInstance() {
        if (m_Allocator) {
            vmaDestroyAllocator(m_Allocator);
        }
    }

    VulkanInstance(const VulkanInstance & other) = delete;
    VulkanInstance(const VulkanInstance && other) noexcept = delete;
    VulkanInstance & operator=(const VulkanInstance & other) = delete;
    VulkanInstance & operator=(const VulkanInstance && other) noexcept = delete;

    void initialize(uint32_t vulkanApiVersion, bool enableValidationLayers, GLFWwindow * window) {
        m_ApiVersion = vulkanApiVersion;
        m_EnableValidationLayers = enableValidationLayers;
        m_Window = window;

        std::vector<const char *> requiredDeviceExtensions = {
            vk::KHRSwapchainExtensionName,
#ifdef __APPLE__
            vk::KHRPortabilitySubsetExtensionName
#endif
        };

        initInstance();
        initDebugMessenger();
        initSurface();
        initPhysicalDevice(requiredDeviceExtensions);
        initDeviceAndQueue(requiredDeviceExtensions);
        initCommandPool();
        // TODO use VMA eventually
        // initAllocator();
    }

    [[nodiscard]] vk::Instance getInstance() const {
        return *m_Instance;
    }

    [[nodiscard]] vk::Device getDevice() const {
        return *m_Device;
    }

    [[nodiscard]] vk::raii::Device & getRaiiDevice() {
        return m_Device;
    }

    [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const {
        return *m_PhysicalDevice;
    }

    [[nodiscard]] vk::SurfaceKHR getSurface() const {
        return *m_Surface;
    }

    [[nodiscard]] uint32_t getQueueIndex() const {
        return m_QueueIndex;
    }

    [[nodiscard]] vk::Queue getGraphicsQueue() const {
        return *m_GraphicsQueue;
    }

    [[nodiscard]] vk::CommandPool getCommandPool() const {
        return *m_CommandPool;
    }

    [[nodiscard]] VmaAllocator & getAllocator() {
        return m_Allocator;
    }

    [[nodiscard]] vk::PhysicalDeviceProperties getPhysicalDeviceProperties() const {
        return m_PhysicalDevice.getProperties();
    }
private:
    void initInstance() {
        // Initialize default vulkan dynamic loader
        // If we're using the vulkan module, the module will do it for us, and we just need to convince our IDE that
        // the code is valid; otherwise, we need to use a macro to do it ourselves.
#ifdef DISABLE_VULKAN_MODULE
        VULKAN_HPP_DEFAULT_DISPATCHER.init();
#else
#ifdef __JETBRAINS_IDE__
        auto & vulkanLoader = dispatchLoaderHack;
#else
        auto & vulkanLoader = vk::detail::defaultDispatchLoaderDynamic;
#endif
        vulkanLoader.init();
#endif

        const vk::ApplicationInfo appInfo {
            .pApplicationName = "Render Engine",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = m_ApiVersion
        };

        std::vector<const char *> requiredLayers {};
        if (m_EnableValidationLayers) {
            const char * validationLayerName = "VK_LAYER_KHRONOS_validation";
            requiredLayers.emplace_back(validationLayerName);
        }

        auto layerProperties = m_Context.enumerateInstanceLayerProperties();
        for (const auto & requiredLayer : requiredLayers) {
            bool available = false;
            for (const auto & availableLayer : layerProperties) {
                if (strcmp(availableLayer.layerName, requiredLayer) == 0) {
                    available = true;
                    break;
                }
            }
            if (!available) {
                throw std::runtime_error(std::string("Could not initialize instance: layer ") + requiredLayer + " not supported!");
            }
        }

        std::vector<const char *> requiredInstanceExtensions {};
        {
            uint32_t glfwExtensionCount = 0;
            auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            requiredInstanceExtensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);

            if (m_EnableValidationLayers) {
                requiredInstanceExtensions.push_back(vk::EXTDebugUtilsExtensionName);
            }
#ifdef __APPLE__
            // portability enumeration extension for MacOS compatibility
            requiredInstanceExtensions.push_back(vk::KHRPortabilityEnumerationExtensionName);
#endif
        }

        auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();
        for (const auto & requiredExtension : requiredInstanceExtensions) {
            bool available = false;
            for (const auto & availableExtension : extensionProperties) {
                if (strcmp(requiredExtension, availableExtension.extensionName) == 0) {
                    available = true;
                    break;
                }
            }
            if (!available) {
                throw std::runtime_error(std::string("Could not initialize instance: extension ") + requiredExtension + " not available.");
            }
        }

        vk::InstanceCreateInfo createInfo {
#ifdef __APPLE__
            .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#endif
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(requiredInstanceExtensions.size()),
            .ppEnabledExtensionNames = requiredInstanceExtensions.data(),
        };

        m_Instance = vk::raii::Instance(m_Context, createInfo);

        // Load function pointers into created instance
#ifdef DISABLE_VULKAN_MODULE
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_Instance);
#else
        vulkanLoader.init(*m_Instance);
#endif
    }

    void initDebugMessenger() {
        // Setup debug messenger if validation is on
        if (m_EnableValidationLayers) {
            vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
            );
            vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
            );
            vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT {
                .messageSeverity = severityFlags,
                .messageType = messageTypeFlags,
                .pfnUserCallback = &debugCallback
            };
            m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
        }
    }

    void initSurface() {
        // Create surface
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(*m_Instance, m_Window, nullptr, &surface) != VkResult::VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface!");
        }
        m_Surface = vk::raii::SurfaceKHR(m_Instance, surface);
    }

    void initPhysicalDevice(std::vector<const char *> & requiredDeviceExtensions) {
        auto physicalDevices = m_Instance.enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");
        }

        auto const devIter = std::ranges::find_if(
            physicalDevices, [&](auto const & physicalDevice) {
                return isDeviceSuitable(physicalDevice, requiredDeviceExtensions);
            }
        );
        if (devIter == physicalDevices.end()) {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }
        m_PhysicalDevice = *devIter;
    }

    bool isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice, std::vector<const char *> const & requiredDeviceExtensions) const {
        bool supportsVulkanApiVersion = physicalDevice.getProperties().apiVersion >= m_ApiVersion;

        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        bool supportsGraphics = false;
        for (const auto & queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
                supportsGraphics = true;
                break;
            }
        }

        auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        bool someExtensionUnavailable = false;
        for (const auto & requiredExtension : requiredDeviceExtensions) {
            bool extensionAvailable = false;
            for (const auto & availableExtension : availableDeviceExtensions) {
                if (strcmp(availableExtension.extensionName, requiredExtension) == 0) {
                    extensionAvailable = true;
                    break;
                }
            }
            if (!extensionAvailable)
                someExtensionUnavailable = true;
        }

        bool supportsAllRequiredExtensions = !someExtensionUnavailable;

        auto features = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                    vk::PhysicalDeviceVulkan11Features,
                                                    vk::PhysicalDeviceVulkan12Features,
                                                    vk::PhysicalDeviceVulkan13Features,
                                                    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        bool supportsRequiredFeatures =
            features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
            features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64 &&
            features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.get<vk::PhysicalDeviceVulkan12Features>().shaderSampledImageArrayNonUniformIndexing &&
            features.get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingVariableDescriptorCount &&
            features.get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingSampledImageUpdateAfterBind &&
            features.get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingPartiallyBound &&
            features.get<vk::PhysicalDeviceVulkan12Features>().runtimeDescriptorArray &&
            features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
            features.get<vk::PhysicalDeviceVulkan12Features>().descriptorIndexing &&
            features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore &&
            features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        return supportsVulkanApiVersion && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }

    void initDeviceAndQueue(std::vector<const char *> & requiredDeviceExtensions) {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_PhysicalDevice.getQueueFamilyProperties();

        // Just using one queue for everything for now for simplicity's sake
        // TODO use multiple queues eventually
        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++) {
            if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
                (queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eCompute) &&
                m_PhysicalDevice.getSurfaceSupportKHR(qfpIndex, *m_Surface)) {
                // found a queue family that supports graphics, compute and present
                m_QueueIndex = qfpIndex;
                break;
            }
        }
        if (m_QueueIndex == ~0) {
            throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
        }

        // Create a chain of feature structures
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        > featureChain = {
            {                                                                   // vk::PhysicalDeviceFeatures2
                .features = {
                    .samplerAnisotropy = true,
                    .shaderInt64 = true
                }
            },
            { .shaderDrawParameters = true },                                 // Enable shader draw parameters from Vulkan 1.1, necessary for shader objects (I think)
            {
                .descriptorIndexing = true,                                     // Enable descriptor indexing for "bindless" uniforms
                .shaderSampledImageArrayNonUniformIndexing = true,
                .descriptorBindingSampledImageUpdateAfterBind = true,
                .descriptorBindingPartiallyBound = true,
                .descriptorBindingVariableDescriptorCount = true,
                .runtimeDescriptorArray = true,
                .timelineSemaphore = true,
                .bufferDeviceAddress = true                                     // Enable accessing buffers via pointers instead of needing descriptors
            },
            { .synchronization2 = true, .dynamicRendering = true },           // Enable dynamic rendering from Vulkan 1.3
            { .extendedDynamicState = true },                                 // Enable extended dynamic state from the extension
        };

        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
            .queueFamilyIndex = m_QueueIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        };

        vk::DeviceCreateInfo deviceCreateInfo {
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data()
        };

        m_Device = vk::raii::Device(m_PhysicalDevice, deviceCreateInfo);

        m_GraphicsQueue = vk::raii::Queue(m_Device, m_QueueIndex, 0);
    }

    // TODO use this
    [[maybe_unused]] void initAllocator() {
        VmaVulkanFunctions vulkanFunctions = {
            .vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(m_Instance.getProcAddr("vkGetInstanceProcAddr")),
            .vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(m_Instance.getProcAddr("vkGetDeviceProcAddr"))
        };

        VmaAllocatorCreateInfo allocatorCreateInfo = {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = *m_PhysicalDevice,
            .device = *m_Device,
            .pVulkanFunctions = &vulkanFunctions,
            .instance = *m_Instance,
            .vulkanApiVersion = m_ApiVersion,
        };

        vmaCreateAllocator(&allocatorCreateInfo, &m_Allocator);
    }

    void initCommandPool() {
        vk::CommandPoolCreateInfo poolInfo {
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_QueueIndex
        };

        m_CommandPool = vk::raii::CommandPool(m_Device, poolInfo);
    }

private:
    uint32_t m_ApiVersion {};
    bool m_EnableValidationLayers {};

    GLFWwindow * m_Window = nullptr;

    vk::raii::Context m_Context {};
    vk::raii::Instance m_Instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
    vk::raii::SurfaceKHR m_Surface = nullptr;
    vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
    vk::raii::Device m_Device = nullptr;

    uint32_t m_QueueIndex = ~0;
    vk::raii::Queue m_GraphicsQueue = nullptr;
    vk::raii::CommandPool m_CommandPool = nullptr;

    VmaAllocator m_Allocator = nullptr;
};
