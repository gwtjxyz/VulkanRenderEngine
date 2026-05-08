module;

#include <cassert>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

export module render_engine;

import std.compat;

import camera;
import vulkan_resource_service;
import render_service_locator;
import render_types;
import resource;
import platform;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

import glm;
import tinyobjloader;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
const std::string MODEL_NAME = "viking_room";
const std::string TEXTURE_NAME = "viking_room";

const std::vector<char const *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif // NDEBUG

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// Upper layer of the renderer, performs rendering and interfaces with the resource manager
export class RenderEngine {
public:
    void run() {
        initService();
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }
private:
    void initService() {
        m_VulkanResourceService = std::make_shared<VulkanResourceService>();
        Locator::provide(m_VulkanResourceService.get());
    }

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, framebufferResizeCallback);
        glfwSetKeyCallback(m_Window, keyCallback);
        glfwSetCursorPosCallback(m_Window, mouseCallback);
        glfwSetScrollCallback(m_Window, scrollCallback);

        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    static void framebufferResizeCallback(GLFWwindow * window, int width, int height) {
        auto app = static_cast<RenderEngine*>(glfwGetWindowUserPointer(window));
        app->m_FramebufferResized = true;
    }

    static void keyCallback(GLFWwindow * window, int key, int scancode, int action, int mods) {
        auto app = static_cast<RenderEngine*>(glfwGetWindowUserPointer(window));
        if (key == GLFW_KEY_R && action == GLFW_PRESS) {
            app->m_ShadersSetToReload = true;
        }
    }

    static void mouseCallback(GLFWwindow * window, double xPos, double yPos) {
        static bool firstMouse = true;
        static float lastX = 0.0f, lastY = 0.0f;

        if (firstMouse) {
            lastX = static_cast<float>(xPos);
            lastY = static_cast<float>(yPos);
            firstMouse = false;
        }

        const float xOffset = static_cast<float>(xPos) - lastX;
        const float yOffset = lastY - static_cast<float>(yPos);   // Inverted: screen Y increases downward, camera pitch increases upward

        lastX = static_cast<float>(xPos);
        lastY = static_cast<float>(yPos);

        auto app = static_cast<RenderEngine*>(glfwGetWindowUserPointer(window));
        app->m_Camera.processMouseMovement(xOffset, yOffset);
    }

    static void scrollCallback(GLFWwindow * window, double xOffset, double yOffset) {
        auto app = static_cast<RenderEngine*>(glfwGetWindowUserPointer(window));
        app->m_Camera.processMouseScroll(static_cast<float>(yOffset));
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createColorResources();
        createDepthResources();
        loadTexture();
        loadModel();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void mainLoop() {
        advanceDeltaTime();

        while (!glfwWindowShouldClose(m_Window)) {
            advanceDeltaTime();
            glfwPollEvents();
            processInput(m_Window, m_Camera, m_DeltaTime);
            if (m_ShadersSetToReload) {
                recompileShadersAndRecreatePipeline();
            }
            drawFrame();
        }

        m_Device.waitIdle();
    }

    // For continuously inputting keys; for them only being pressed once, we keep the callback
    void processInput(GLFWwindow * window, Camera & camera, const float deltaTime) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::RIGHT, deltaTime);

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::DOWN, deltaTime);

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
    }

    void cleanup() {
        cleanupSwapChain();

        // Render-specific image views
        const vk::Device deviceHandle = *m_Device;

        deviceHandle.destroyImageView(m_DepthImageView);
        deviceHandle.destroyImage(m_DepthImage);
        deviceHandle.freeMemory(m_DepthImageMemory);

        deviceHandle.destroyImageView(m_ColorImageView);
        deviceHandle.destroyImage(m_ColorImage);
        deviceHandle.freeMemory(m_ColorImageMemory);

        for (const auto & uniformBuffer : m_UniformBuffers) {
            deviceHandle.unmapMemory(uniformBuffer.bufferMemory);
            deviceHandle.freeMemory(uniformBuffer.bufferMemory);
            deviceHandle.destroyBuffer(uniformBuffer.buffer);
        }

        m_ResourceManager.unloadAll();

        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void drawFrame() {
        // Note: m_InFlightFences, m_PresentCompleteSemaphores and m_CommandBuffers are indexed by frameIndex,
        // while m_RenderFinishedSemaphores is indexed by imageIndex

        auto fenceResult = m_Device.waitForFences(*m_InFlightFences[m_FrameIndex], vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for fence!");
        }

        auto [result, imageIndex] = m_SwapChain.acquireNextImage(UINT64_MAX, *m_PresentCompleteSemaphores[m_FrameIndex], nullptr);

        // TODO: do we need to define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS somehow?
        if (result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
            throw std::runtime_error("Failed to acquire swap chain image!");
        }

        // Only reset the fence if we are submitting work
        m_Device.resetFences(*m_InFlightFences[m_FrameIndex]);

        m_CommandBuffers[m_FrameIndex].reset();
        recordCommandBuffer(imageIndex);
        updateUniformBuffer(m_FrameIndex);

        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_PresentCompleteSemaphores[m_FrameIndex],             // wait for this semaphore before executing
            .pWaitDstStageMask = &waitDestinationStageMask,                             // which pipeline stage to wait on
            .commandBufferCount = 1,
            .pCommandBuffers = &*m_CommandBuffers[m_FrameIndex],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*m_RenderFinishedSemaphores[imageIndex]             // which semaphore to signal after executing
        };
        m_GraphicsQueue.submit(submitInfo, *m_InFlightFences[m_FrameIndex]); // <-- signals m_DrawFence once command execution is complete

        const vk::PresentInfoKHR presentInfoKHR = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_RenderFinishedSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*m_SwapChain,
            .pImageIndices = &imageIndex
        };
        result = m_GraphicsQueue.presentKHR(presentInfoKHR);
        if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || m_FramebufferResized) {
            m_FramebufferResized = false;
            recreateSwapChain();
        } else {
            // There are no other success codes other than eSuccess; on any error code, presentKHR already threw an exception.
            assert(result == vk::Result::eSuccess);
        }
        m_FrameIndex = (m_FrameIndex+1) % MAX_FRAMES_IN_FLIGHT;
    }

    void advanceDeltaTime() {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        m_DeltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
        // std::println("m_DeltaTime: {}", m_DeltaTime);
        startTime = currentTime;
    }

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo {};
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, -2.0f));
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        ubo.model = glm::rotate(model, time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = m_Camera.getViewMatrix();
        ubo.proj = m_Camera.getProjectionMatrix();
        ubo.proj[1][1] *= -1; // Vulkan's Y coordinate is inverted compared to OpenGL's, which glm was designed for originally
        memcpy(m_UniformBuffers[currentImage].mappedMemory, &ubo, sizeof(ubo));
    }

    void createInstance() {
        constexpr vk::ApplicationInfo appInfo {
            .pApplicationName = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION( 1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION( 1, 0, 0),
            .apiVersion = vk::ApiVersion14
        };

        // Get the required layers
        // Can configure validation layers further using vk_layer_settings.txt inside Vulkan SDK's Config folder
        std::vector<char const *> requiredLayers;
        if (enableValidationLayers) {
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        }

        // Check if the required layers are supported by the Vulkan implementation
        auto layerProperties = m_Context.enumerateInstanceLayerProperties();
        // TODO learn lambdas better so I can write things like these myself
        if (std::ranges::any_of(requiredLayers, [&layerProperties](auto const & requiredLayer) {
            return std::ranges::none_of(layerProperties,
                                    [requiredLayer](auto const & layerProperty)
                                         { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
        }))
        {
            throw std::runtime_error("One or more required layers are not supported!");
        }

        // Get required extensions
        auto requiredExtensions = getRequiredInstanceExtensions();

        // Check if the required extensions are supported by the Vulkan implementation
        auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();

        auto unsupportedPropertyIt =
            std::ranges::find_if(requiredExtensions, [&extensionProperties](auto const & requiredExtension) {
                return std::ranges::none_of(extensionProperties, [requiredExtension](auto const & extensionProperty) {
                    return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
                });
            });
        if (unsupportedPropertyIt != requiredExtensions.end()) {
            throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
        }

        vk::InstanceCreateInfo createInfo {
#ifdef __APPLE__
            .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#endif
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
            .ppEnabledExtensionNames = requiredExtensions.data(),
        };

        m_Instance = vk::raii::Instance(m_Context, createInfo);
    }

    void setupDebugMessenger() {
        if (!enableValidationLayers) return;

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags( vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError );
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags( vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation );
        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT {
            .messageSeverity = severityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = &debugCallback
        };
        m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
    }

    static std::vector<const char *> getRequiredInstanceExtensions() {
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }
#ifdef __APPLE__
        // portability enumeration extension for MacOS compatibility
        extensions.push_back(vk::KHRPortabilityEnumerationExtensionName);
#endif
        return extensions;
    }

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*
    ) {
        std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

        return vk::False;
    }

    void createSurface() {
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(*m_Instance, m_Window, nullptr, &surface) != VkResult::VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface!");
        }
        m_Surface = vk::raii::SurfaceKHR(m_Instance, surface);
    }

    void pickPhysicalDevice() {
        auto physicalDevices = m_Instance.enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");
        }
        auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const & physicalDevice) {
            return isDeviceSuitable(physicalDevice);
        });
        if (devIter == physicalDevices.end()) {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }
        m_PhysicalDevice = *devIter;
        m_VulkanResourceService->setPhysicalDevice(m_PhysicalDevice);
        m_MsaaSamples = getMaxUsableSampleCount();
    }

    bool isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice) {
        bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const &qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

        auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();

        bool supportsAllRequiredExtensions = std::ranges::all_of(m_RequiredDeviceExtensions, [&availableDeviceExtensions](auto const & requiredDeviceExtension) {
            return std::ranges::any_of(availableDeviceExtensions, [requiredDeviceExtension](const auto & availableDeviceExtension) {
                return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
            });
        });

        auto features = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                                            vk::PhysicalDeviceVulkan11Features,
                                                                            vk::PhysicalDeviceVulkan13Features,
                                                                            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        bool supportsRequiredFeatures =
            features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
            features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }

    vk::SampleCountFlagBits getMaxUsableSampleCount() {
        vk::PhysicalDeviceProperties physicalDeviceProperties = m_PhysicalDevice.getProperties();

        vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; };
        if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; };
        if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; };
        if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; };
        if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; };
        if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; };

        return vk::SampleCountFlagBits::e1;
    }

    void createLogicalDevice() {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_PhysicalDevice.getQueueFamilyProperties();

        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++) {
            if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
                m_PhysicalDevice.getSurfaceSupportKHR(qfpIndex, *m_Surface)) {
                // found a queue family that supports both graphics and present
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
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        > featureChain = {
            {.features = {.samplerAnisotropy = true}},                   // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},                                 // Enable shader draw parameters from Vulkan 1.1, necessary for shader objects (I think)
            {.synchronization2 = true, .dynamicRendering = true},           // Enable dynamic rendering from Vulkan 1.3
            {.extendedDynamicState = true},                                 // Enable extended dynamic state from the extension
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
            .enabledExtensionCount = static_cast<uint32_t>(m_RequiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = m_RequiredDeviceExtensions.data()
        };

        m_Device = vk::raii::Device(m_PhysicalDevice, deviceCreateInfo);
        m_VulkanResourceService->setDevice(m_Device);
        m_GraphicsQueue = vk::raii::Queue(m_Device, m_QueueIndex, 0);
        m_VulkanResourceService->setGraphicsQueue(m_GraphicsQueue);
    }

    void createSwapChain() {
        vk::SurfaceCapabilitiesKHR surfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(*m_Surface);
        m_SwapChainExtent = chooseSwapExtent(surfaceCapabilities);
        uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = m_PhysicalDevice.getSurfaceFormatsKHR(*m_Surface);
        m_SwapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

        std::vector<vk::PresentModeKHR> availablePresentModes = m_PhysicalDevice.getSurfacePresentModesKHR(*m_Surface);

        vk::SwapchainCreateInfoKHR swapChainCreateInfo = {
            .surface = *m_Surface,
            .minImageCount = minImageCount,
            .imageFormat = m_SwapChainSurfaceFormat.format,
            .imageColorSpace = m_SwapChainSurfaceFormat.colorSpace,
            .imageExtent = m_SwapChainExtent,
            .imageArrayLayers = 1,                                          // always 1 unless developing stereoscopic 3D app
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,      // rendering directly to swap chain images; can use eTransferDst if we want to do post-processing first
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = chooseSwapPresentMode(availablePresentModes),
            .clipped = true
        };

        m_SwapChain = vk::raii::SwapchainKHR(m_Device, swapChainCreateInfo);
        m_SwapChainImages = m_SwapChain.getImages();

        // Set aspect ratio for camera
        m_Camera.updateAspectRatio(
            static_cast<float>(m_SwapChainExtent.width) / static_cast<float>(m_SwapChainExtent.height)
        );
    }

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const & availableFormats) {
        const auto formatIt = std::ranges::find_if(
            availableFormats,
            [](const auto & format) {
            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
    }

    static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const & availablePresentModes) {
        assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
        return std::ranges::any_of(availablePresentModes, [](const vk::PresentModeKHR value) {
            return vk::PresentModeKHR::eMailbox == value;
        }) ? vk::PresentModeKHR::eMailbox
        : vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const & capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width, height;
        glfwGetFramebufferSize(m_Window, &width, &height);

        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const & surfaceCapabilities) {
        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;
    }

    void createImageViews() {
        assert(m_SwapChainImageViews.empty());

        for (auto & image : m_SwapChainImages) {
            m_SwapChainImageViews.emplace_back(createImageView(image, m_SwapChainSurfaceFormat.format, vk::ImageAspectFlagBits::eColor, 1));
        }
    }

    [[nodiscard]]
    vk::raii::ImageView createImageView(const vk::Image & image, const vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels) const {
        vk::ImageViewCreateInfo viewInfo = {
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = { aspectFlags, 0, mipLevels, 0, 1 }
        };
        return {m_Device, viewInfo};
    }

    void createDescriptorSetLayout() {
        std::array bindings = {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo = {
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };

        m_DescriptorSetLayout = vk::raii::DescriptorSetLayout(m_Device, layoutInfo);
    }

    void createGraphicsPipeline() {
        vk::raii::ShaderModule shaderModule = createShaderModule(readFile("shaders/slang.spv"));

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };

        // can also add .pSpecializationInfo parameter to specify and optimize shader constants
        vk::PipelineShaderStageCreateInfo vertShaderStageInfo {
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shaderModule,
            .pName = "vertMain"
        };
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo {
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shaderModule,
            .pName = "fragMain"
        };
        vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly {.topology = vk::PrimitiveTopology::eTriangleList};

        std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        // don't need to specify viewports and scissors now as we are going to be setting them dynamically
        vk::PipelineViewportStateCreateInfo viewportState {.viewportCount = 1, .scissorCount =  1};

        vk::PipelineRasterizationStateCreateInfo rasterizer {
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise, // counter-clockwise since we flip the Y-axis of the projection matrix
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f
        };

        vk::PipelineMultisampleStateCreateInfo multisampling {
            .rasterizationSamples = m_MsaaSamples,
            .sampleShadingEnable = vk::False
        };

        vk::PipelineDepthStencilStateCreateInfo depthStencil {
            .depthTestEnable = vk::True,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False
        };

        vk::PipelineColorBlendAttachmentState colorBlendAttachment {
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        vk::PipelineColorBlendStateCreateInfo colorBlending {
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
            .setLayoutCount = 1,
            .pSetLayouts = &*m_DescriptorSetLayout,
            .pushConstantRangeCount = 0
        };
        m_PipelineLayout = vk::raii::PipelineLayout(m_Device, pipelineLayoutInfo);

        vk::Format depthFormat = findDepthFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &m_SwapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat
        };
        vk::GraphicsPipelineCreateInfo pipelineInfo {
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = m_PipelineLayout,
            .renderPass = nullptr                   // we're using dynamic rendering instead of render passes
        };

        m_GraphicsPipeline = vk::raii::Pipeline(m_Device, nullptr, pipelineInfo);
    }

    [[nodiscard]]
    vk::raii::ShaderModule createShaderModule(const std::vector<char> & code) const {
        vk::ShaderModuleCreateInfo createInfo {
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };

        vk::raii::ShaderModule shaderModule { m_Device, createInfo };
        return shaderModule;
    }

    void createCommandPool() {
        vk::CommandPoolCreateInfo poolInfo {
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_QueueIndex
        };

        m_CommandPool = vk::raii::CommandPool(m_Device, poolInfo);
        m_VulkanResourceService->setCommandPool(m_CommandPool);
    }

    void createColorResources() {
        VulkanImageData colorResources = m_VulkanResourceService->createColorResources(
            m_SwapChainSurfaceFormat.format, m_SwapChainExtent, m_MsaaSamples
        );
        m_ColorImage = colorResources.image;
        m_ColorImageMemory = colorResources.imageMemory;
        m_ColorImageView = colorResources.imageView;
    }

    void createDepthResources() {
        vk::Format depthFormat = findDepthFormat();
        VulkanImageData depthResources = m_VulkanResourceService->createDepthResources(
            depthFormat, m_SwapChainExtent, m_MsaaSamples
        );
        m_DepthImage = depthResources.image;
        m_DepthImageMemory = depthResources.imageMemory;
        m_DepthImageView = depthResources.imageView;
    }

    vk::Format findDepthFormat() {
        return findSupportedFormat(
            {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment
        );
    }

    vk::Format findSupportedFormat(const std::vector<vk::Format> & candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
        for (const auto format : candidates) {
            vk::FormatProperties props = m_PhysicalDevice.getFormatProperties(format);
            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("Failed to find supported format!");
    }

    // TODO remove? (unused)
    static bool hasStencilComponent(vk::Format format) {
        return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
    }

    void loadTexture() {
        auto textureHandle = m_ResourceManager.load<Texture>(TEXTURE_NAME);
    }

    void loadModel() {
        auto modelHandle = m_ResourceManager.load<Mesh>(MODEL_NAME);
    }

    vk::raii::CommandBuffer beginSingleTimeCommands() {
        vk::CommandBufferAllocateInfo allocInfo = {
            .commandPool = m_CommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        vk::raii::CommandBuffer commandBuffer = std::move(m_Device.allocateCommandBuffers(allocInfo).front());

        vk::CommandBufferBeginInfo beginInfo { .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
        commandBuffer.begin(beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(vk::raii::CommandBuffer & commandBuffer) {
        commandBuffer.end();

        vk::SubmitInfo submitInfo {.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer };
        m_GraphicsQueue.submit(submitInfo, nullptr);
        // using a fence instead of waitIdle() would allow us to schedule multiple transfer simultaneously and
        // wait for all of them to complete instead of executing one at a time. ( = likely better optimization)
        m_GraphicsQueue.waitIdle();
    }

    void createUniformBuffers() {
        m_UniformBuffers = m_VulkanResourceService->createUniformBuffers(
            sizeof(UniformBufferObject), MAX_FRAMES_IN_FLIGHT
        );
    }

    void createDescriptorPool() {
        std::array poolSize = {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)
        };
        vk::DescriptorPoolCreateInfo poolInfo = {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
            .pPoolSizes = poolSize.data()
        };
        m_DescriptorPool = vk::raii::DescriptorPool(m_Device, poolInfo);
    }

    void createDescriptorSets() {
        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *m_DescriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo {
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts = layouts.data()
        };
        m_DescriptorSets.clear();
        m_DescriptorSets = m_Device.allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::DescriptorBufferInfo bufferInfo = {
                .buffer = m_UniformBuffers[i].buffer,
                .offset = 0,
                .range = sizeof(UniformBufferObject), // can also use vk::WholeSize since we're overwriting the whole buffer
            };

            auto textureImage = m_ResourceManager.getResource<Texture>(TEXTURE_NAME);

            vk::DescriptorImageInfo imageInfo = {
                .sampler = textureImage->getSampler(),
                .imageView = textureImage->getImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            };

            // Note: eCombinedImageSampler is not necessarily the most optimal type to use,
            // but sometimes it is, and also it's the most straightforward to set up
            std::array descriptorWrites {
                vk::WriteDescriptorSet {
                    .dstSet = m_DescriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet = m_DescriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo
                }
            };
            m_Device.updateDescriptorSets(descriptorWrites, {});
        }
    }

    void createCommandBuffers() {
        vk::CommandBufferAllocateInfo allocInfo {
            .commandPool = m_CommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };

        m_CommandBuffers = vk::raii::CommandBuffers(m_Device, allocInfo);
    }

    void recordCommandBuffer(uint32_t imageIndex) {
        auto & commandBuffer = m_CommandBuffers[m_FrameIndex];
        commandBuffer.begin( {} );

        // Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        transitionImageLayout(
            m_SwapChainImages[imageIndex],
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor
        );
        transitionImageLayout(
            m_ColorImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor
        );
        // Transition depth image to DEPTH_ATTACHMENT_OPTIMAL
        transitionImageLayout(
            m_DepthImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth
        );

        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

        // Multisampled color attachment with resolve attachment
        // According to spec, if resolveMode is not RESOLVE_MODE_NONE, and resolveImageView is not null,
        // a render pass multisample resolve operation is defined for the attachment subresource.
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = m_ColorImageView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .resolveMode = vk::ResolveModeFlagBits::eAverage,
            .resolveImageView = m_SwapChainImageViews[imageIndex],
            .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor
        };
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = m_DepthImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .clearValue = clearDepth
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = { .offset = { 0, 0 }, .extent = m_SwapChainExtent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo
        };

        commandBuffer.beginRendering(renderingInfo);

        // TODO: is this efficient to do every time we record the command buffer? (I assume not)
        const auto vikingRoomMesh = m_ResourceManager.getResource<Mesh>(MODEL_NAME);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_GraphicsPipeline);
        commandBuffer.bindVertexBuffers(0, vikingRoomMesh->getVertexBuffer(), {0});
        commandBuffer.bindIndexBuffer(vikingRoomMesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
        commandBuffer.setViewport(0, vk::Viewport(
            0.0f, 0.0f,
            static_cast<float>(m_SwapChainExtent.width), static_cast<float>(m_SwapChainExtent.height),
            0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapChainExtent));
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_PipelineLayout, 0, *m_DescriptorSets[m_FrameIndex], nullptr);
        commandBuffer.drawIndexed(vikingRoomMesh->getIndexCount(), 1, 0, 0, 0);

        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to PRESENT_SRC
        transitionImageLayout(
            m_SwapChainImages[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::ImageAspectFlagBits::eColor
        );

        commandBuffer.end();
    }

    // Keeping this here isntead of moving it to the resource service for now because this is used
    // For recording command buffers for drawing, not necessarily resource allocation
    void transitionImageLayout(
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask,
        vk::ImageAspectFlags imageAspectFlags
    ) {
        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = {
                .aspectMask = imageAspectFlags,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vk::DependencyInfo dependencyInfo = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        m_CommandBuffers[m_FrameIndex].pipelineBarrier2(dependencyInfo);
    }

    void createSyncObjects() {
        assert(m_PresentCompleteSemaphores.empty() && m_RenderFinishedSemaphores.empty() && m_InFlightFences.empty());

        for (size_t i = 0; i < m_SwapChainImages.size(); ++i) {
            m_RenderFinishedSemaphores.emplace_back(m_Device, vk::SemaphoreCreateInfo());
        }

        for (size_t i = 0; i < m_SwapChainImages.size(); ++i) {
            m_PresentCompleteSemaphores.emplace_back(m_Device, vk::SemaphoreCreateInfo());
            m_InFlightFences.emplace_back(m_Device, vk::FenceCreateInfo {.flags = vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void cleanupSwapChain() {
        m_SwapChainImageViews.clear();
        m_SwapChain = nullptr; // doing this automatically calls the destructor which in turn calls swapChain.clear()
    }

    void recreateSwapChain() {
        // Note: it's possible to do this differently - here we need to stop all renderings before creating
        // the new swap chain. Alternatively, we can create a new swap chain while still drawing to the old swap chain
        // image by passing the old swap chain object in the `oldSwapChain` field in `vk::SwapchainCreateInfoKHR` and
        // destroying the old swap chain as soon as we're fully done with it. Our current approach is technically
        // less optimal than what is outlined above, but it is simpler, so we're sticking with it for now.

        int width = 0, height = 0;
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_Window, &width, &height);
            glfwWaitEvents();
        }

        m_Device.waitIdle();

        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        createColorResources();
        createDepthResources();
    }

    void recompileShadersAndRecreatePipeline() {
        m_Device.waitIdle();

        compileShader("shaders/shader.slang", "shaders/slang.spv");
        m_GraphicsPipeline.clear();
        createGraphicsPipeline();
        m_ShadersSetToReload = false;
    }
public:
    // public variables go here
private:
    std::shared_ptr<VulkanResourceService> m_VulkanResourceService = nullptr;
    ResourceManager m_ResourceManager {};

    GLFWwindow * m_Window = nullptr;

    Camera m_Camera {};

    float m_DeltaTime = 0.0f;

    vk::raii::Context m_Context;
    vk::raii::Instance m_Instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
    vk::raii::SurfaceKHR m_Surface = nullptr;
    vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
    uint32_t m_QueueIndex = ~0;
    vk::raii::Device m_Device = nullptr;
    vk::raii::Queue m_GraphicsQueue = nullptr;

    vk::raii::SwapchainKHR m_SwapChain = nullptr;
    std::vector<vk::Image> m_SwapChainImages;
    vk::SurfaceFormatKHR m_SwapChainSurfaceFormat;
    vk::Extent2D m_SwapChainExtent;
    std::vector<vk::raii::ImageView> m_SwapChainImageViews;
    bool m_FramebufferResized = false;

    vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout m_PipelineLayout = nullptr;
    vk::raii::Pipeline m_GraphicsPipeline = nullptr;
    bool m_ShadersSetToReload = false;

    vk::raii::CommandPool m_CommandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> m_CommandBuffers {};

    std::vector<vk::raii::Semaphore> m_PresentCompleteSemaphores {};
    std::vector<vk::raii::Semaphore> m_RenderFinishedSemaphores {};
    std::vector<vk::raii::Fence> m_InFlightFences {};
    uint32_t m_FrameIndex = 0;

    // Note: the below setup is far from optimal; ideally we should allocate a single buffer
    // and store both our vertex and index data in there, rather than creating individual buffers.
    // It's also good to use a memory allocator on top of it all, like VMA. We are not doing it
    // here because it would be slightly overkill for drawing just a couple simple shapes.
    // We'll optimize this once there's an actual need/reason to do so.

    std::vector<VulkanUniformBufferData> m_UniformBuffers {};

    vk::raii::DescriptorPool m_DescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> m_DescriptorSets;

    vk::Image m_DepthImage = nullptr;
    vk::DeviceMemory m_DepthImageMemory = nullptr;
    vk::ImageView m_DepthImageView = nullptr;

    vk::Image m_ColorImage = nullptr;
    vk::DeviceMemory m_ColorImageMemory = nullptr;
    vk::ImageView m_ColorImageView = nullptr;

    vk::SampleCountFlagBits m_MsaaSamples = vk::SampleCountFlagBits::e1;

    std::vector<const char *> m_RequiredDeviceExtensions = {
        vk::KHRSwapchainExtensionName
#ifdef __APPLE__
        ,vk::KHRPortabilitySubsetExtensionName
#endif
    };
};