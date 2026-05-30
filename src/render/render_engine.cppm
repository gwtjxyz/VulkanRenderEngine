module;

#include <cassert>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

#ifdef DISABLE_IMPORT_STD
#include <iostream>
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

export module render_engine;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import camera;
import ecs;
import hlsl_compiler;
import render_service_locator;
import render_types;
import resource;
import platform;
import vulkan_resource_service;

import glm;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

// CLion hack to fix ambiguous symbol errors
// This doesn't actually do anything, but convinces CLion that there are no ambiguous symbols in the code
// Otherwise the code compiles and runs with no issues, but the IDE complains regardless
#ifdef __JETBRAINS_IDE__
class DispatchLoaderHack {
public:
    void init() {}
    void init(const vk::Instance & instance) { (void)instance; }
};

DispatchLoaderHack dispatchLoaderHack;
#endif

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr uint32_t PARTICLE_COUNT = 8192;
const std::string VIKING_ROOM_MODEL_NAME = "viking_room";
const std::string VIKING_ROOM_TEXTURE_NAME = "viking_room";
const std::string TERRAIN_MODEL_NAME = "terrain";
const std::string TERRAIN_TEXTURE_NAME = "terrain_diffuse";

const std::vector<char const *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif // NDEBUG

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

static void checkVkResult(VkResult err) {
    if (err == 0)
        return;
    std::cerr << "[vulkan] Error: VkResult = " << err << std::endl;
    // TODO: don't know if we really want to crash on any Vulkan error inside ImGui
    if (err < 0)
        std::abort();
}

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
        glfwSetMouseButtonCallback(m_Window, mouseButtonCallback);
        glfwSetWindowFocusCallback(m_Window, windowFocusCallback);
        glfwSetCursorPosCallback(m_Window, mouseCallback);
        glfwSetScrollCallback(m_Window, scrollCallback);

        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    static void framebufferResizeCallback(GLFWwindow * window, int width, int height) {
        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
        app->m_FramebufferResized = true;
    }

    static void keyCallback(GLFWwindow * window, int key, int scancode, int action, int mods) {
        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
        if (key == GLFW_KEY_R && action == GLFW_PRESS) {
            app->m_ShadersSetToReload = true;
        }
    }

    static void mouseButtonCallback(GLFWwindow * window, int button, int action, int mods) {
        ImGuiIO & io = ImGui::GetIO();
        if (io.WantCaptureMouse == false && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
            app->m_ShouldCursorBeVisible = false;
        }
    }

    static void windowFocusCallback(GLFWwindow * window, int focused) {
        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));

        if (focused) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            app->m_IsWindowFocused = true;
        } else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            app->m_IsWindowFocused = false;
        }
    }

    static void mouseCallback(GLFWwindow * window, double xPos, double yPos) {
        static bool firstMouse = true;
        static float lastX = 0.0f, lastY = 0.0f;

        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));

        // Still not quite right - camera jumps like crazy after regaining focus
        if (firstMouse || !app->m_IsWindowFocused) {
            lastX = static_cast<float>(xPos);
            lastY = static_cast<float>(yPos);
            firstMouse = false;
        }

        const float xOffset = static_cast<float>(xPos) - lastX;
        const float yOffset = lastY - static_cast<float>(yPos);   // Inverted: screen Y increases downward, camera pitch increases upward

        lastX = static_cast<float>(xPos);
        lastY = static_cast<float>(yPos);


        // Still want to update lastX and lastY so we don't get a massive jump when we stop hovering over
        // an imgui window, but don't want to route it to our camera controls
        if (RenderEngine::isImguiCapturingMouse() || app->m_ShouldCursorBeVisible) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        app->m_Camera.processMouseMovement(xOffset, yOffset);
    }

    static void scrollCallback(GLFWwindow * window, double xOffset, double yOffset) {
        ImGuiIO & io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }

        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
        app->m_Camera.processMouseScroll(static_cast<float>(yOffset));
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        setupImgui();
        createImageViews();
        createCommandPool();
        loadTextures();
        loadModels();
        createDescriptorSetLayout();
        createComputePipeline();
        createGraphicsPipeline();
        createPointGraphicsPipeline();
        createColorResources();
        createDepthResources();
        createShaderDataBuffers();
        createComputeBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createComputeCommandBuffers();
        createSyncObjects();
    }

    void mainLoop() {
        advanceDeltaTime();

        uint32_t frameCount = 0;
        while (!glfwWindowShouldClose(m_Window)) {
            auto timeAtFrameStart = std::chrono::high_resolution_clock::now();
            static auto timeSinceLastFpsMeasurement = timeAtFrameStart;

            advanceDeltaTime();

            glfwPollEvents();
            processInput(m_Window, m_Camera, m_DeltaTime);

            if (m_ShadersSetToReload) {
                recompileShadersAndRecreatePipeline();
            }
            drawFrame();

            auto timeAtFrameEnd = std::chrono::high_resolution_clock::now();
            m_FrameTimeMs = std::chrono::duration<float, std::chrono::milliseconds::period>(timeAtFrameEnd - timeAtFrameStart).count();

            frameCount++;
            if (timeAtFrameEnd - timeSinceLastFpsMeasurement >= std::chrono::duration(std::chrono::seconds(1))) {
                m_Fps = frameCount;
                frameCount = 0;
                timeSinceLastFpsMeasurement = timeAtFrameEnd;
            }
        }

        m_Device.waitIdle();
    }

    static bool isImguiCapturingKeyboard() {
        const ImGuiIO & io = ImGui::GetIO();

        return io.WantCaptureKeyboard;
    }

    static bool isImguiCapturingMouse() {
        const ImGuiIO & io = ImGui::GetIO();

        return io.WantCaptureMouse;
    }

    // For continuously inputting keys; for them only being pressed once, we keep the callback
    void processInput(GLFWwindow * window, Camera & camera, const float deltaTime) {
        if (isImguiCapturingKeyboard())
            return;

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

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && m_CanUiFocusBeToggled) {
            m_ShouldCursorBeVisible = !m_ShouldCursorBeVisible;
            m_CanUiFocusBeToggled = false;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE && !m_CanUiFocusBeToggled) {
            m_CanUiFocusBeToggled = true;
        }
    }

    void cleanup() {
        cleanupSwapChain();

        // Render-specific image views
        const vk::Device deviceHandle = *m_Device;

        m_VulkanResourceService->freeResources(m_DepthImage, m_DepthImageMemory, m_DepthImageView);
        m_VulkanResourceService->freeResources(m_ColorImage, m_ColorImageMemory, m_ColorImageView);

        for (const auto & shaderDataBuffer : m_ShaderDataBuffers) {
            deviceHandle.unmapMemory(shaderDataBuffer.bufferMemory);
            deviceHandle.freeMemory(shaderDataBuffer.bufferMemory);
            deviceHandle.destroyBuffer(shaderDataBuffer.buffer);
        }

        for (const auto & computeDataBuffer : m_ComputeDataBuffers) {
            deviceHandle.freeMemory(computeDataBuffer.bufferMemory);
            deviceHandle.destroyBuffer(computeDataBuffer.buffer);
        }

        m_ResourceManager.unloadAll();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void drawFrame() {
        // Start the ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Draw our custom UI widget
        drawUI();

        auto [result, imageIndex] = m_SwapChain.acquireNextImage(UINT64_MAX, nullptr, *m_InFlightFences[m_FrameIndex]);
        auto fenceResult = m_Device.waitForFences(*m_InFlightFences[m_FrameIndex], vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for fence!");
        }

        // Only reset the fence if we are submitting work
        m_Device.resetFences(*m_InFlightFences[m_FrameIndex]);

        // Update timeline value for this frame
        uint64_t computeWaitValue = m_TimelineValue;
        uint64_t computeSignalValue = ++m_TimelineValue;
        uint64_t graphicsWaitValue = computeSignalValue;
        uint64_t graphicsSignalValue = ++m_TimelineValue;

        updateComputePushConstants();
        updateShaderData();

        // Compute
        {
            // If particles are toggled off, don't compute or render anything
            // Still need to "submit" work after in order to signal semaphores, otherwise
            // the following stages would wait forever and freeze the program
            if (m_ParticlesEnabled) {
                recordComputeCommandBuffer();
            }
            // Submit compute work
            vk::TimelineSemaphoreSubmitInfo computeTimelineInfo = {
                .waitSemaphoreValueCount = 1,
                .pWaitSemaphoreValues = &computeWaitValue,
                .signalSemaphoreValueCount = 1,
                .pSignalSemaphoreValues = &computeSignalValue
            };

            vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eComputeShader };

            vk::SubmitInfo computeSubmitInfo = {
                .pNext = &computeTimelineInfo,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*m_Semaphore,
                .pWaitDstStageMask = waitStages,
                .commandBufferCount = 1,
                .pCommandBuffers = &*m_ComputeCommandBuffers[m_FrameIndex],
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &*m_Semaphore
            };

            m_GraphicsQueue.submit(computeSubmitInfo, nullptr);
        }

        // Graphics
        {
            recordCommandBuffer(imageIndex);

            // Submit graphics work (wait for compute to finish)
            vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eVertexInput);

            vk::TimelineSemaphoreSubmitInfo graphicsTimelineInfo = {
                .waitSemaphoreValueCount = 1,
                .pWaitSemaphoreValues = &graphicsWaitValue,
                .signalSemaphoreValueCount = 1,
                .pSignalSemaphoreValues = &graphicsSignalValue
            };

            const vk::SubmitInfo graphicsSubmitInfo = {
                .pNext = &graphicsTimelineInfo,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*m_Semaphore,
                .pWaitDstStageMask = &waitDestinationStageMask,
                .commandBufferCount = 1,
                .pCommandBuffers = &*m_CommandBuffers[m_FrameIndex],
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &*m_Semaphore
            };

            m_GraphicsQueue.submit(graphicsSubmitInfo, nullptr);

            // Present the image (wait for graphics to finish)
            vk::SemaphoreWaitInfo waitInfo = {
                .semaphoreCount = 1,
                .pSemaphores = &*m_Semaphore,
                .pValues = &graphicsSignalValue
            };
            // Wait for graphics to complete before presenting
            result = m_Device.waitSemaphores(waitInfo, UINT64_MAX);
            if (result != vk::Result::eSuccess) {
                throw std::runtime_error("failed to wait for semaphores!");
            }

            const vk::PresentInfoKHR presentInfoKHR = {
                .waitSemaphoreCount = 0,                                        // No binary semaphores needed
                .pWaitSemaphores = nullptr,
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
        }

        m_FrameIndex = (m_FrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // In the future we will extract it to somewhere else (possibly a separate class/system?),
    // but for now, this isn't really required
    void drawUI() {
        // Initial window size
        const ImGuiViewport * mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + 20, mainViewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 210), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Engine Controls", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
            // Early return if the window is collapsed
            ImGui::End();
            return;
        }

        const float labelWidthBase = ImGui::GetFontSize() * 12;
        const float labelWidthMax = ImGui::GetContentRegionAvail().x * 0.4f;
        float labelWidth;
        if (labelWidthBase > labelWidthMax) {
            labelWidth = labelWidthMax;
        } else {
            labelWidth = labelWidthBase;
        }

        ImGui::PushItemWidth(-labelWidth);
        ImGui::Text("FPS: %lld", m_Fps);
        ImGui::Text("Frametime: %.4f ms", m_FrameTimeMs);
        ImGui::Spacing();
        ImGui::Text("Controls: WASD to move, Space/Control to move up/down");
        ImGui::Text("Esc to show/hide mouse, Q to exit, R to reload shaders");
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Shader Controls")) {
            if (ImGui::BeginTable("ShaderControlCheckboxes", 2)) {
                ImGui::TableNextColumn();
                ImGui::Checkbox("Lighting enabled", &m_IsLightingEnabled);
                ImGui::Checkbox("Model spin enabled", &m_IsModelSpinEnabled);
                ImGui::TableNextColumn();
                ImGui::Checkbox("Particles enabled", &m_ParticlesEnabled);
                ImGui::EndTable();
            }
            ImGui::Spacing();
            static float lightPosWidgetData[3] {
                m_LightPosition.x, m_LightPosition.y, m_LightPosition.z
            };
            if (!m_IsLightingEnabled) {
                ImGui::BeginDisabled();
            }

            ImGui::DragFloat3("Light position", lightPosWidgetData, 0.005f, -20.0f, 20.0f, "%.3f");
            m_LightPosition.x = lightPosWidgetData[0];
            m_LightPosition.y = lightPosWidgetData[1];
            m_LightPosition.z = lightPosWidgetData[2];

            if (!m_IsLightingEnabled) {
                ImGui::EndDisabled();
            }
        }

        ImGui::End();
    }

    void advanceDeltaTime() {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        m_DeltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
        startTime = currentTime;
    }

    void updateShaderData() const {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        ShaderData shaderData[2] {};
        static glm::mat4 vikingRoomModel = glm::rotate(
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, -0.5f, -2.0f)
            ),
            glm::radians(-90.0f),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );

        if (m_IsModelSpinEnabled) {
            vikingRoomModel = glm::rotate(vikingRoomModel, time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        }

        // Viking room
        shaderData[0].model = vikingRoomModel;
        shaderData[0].view = m_Camera.getViewMatrix();
        shaderData[0].projection = m_Camera.getProjectionMatrix();
        shaderData[0].projection[1][1] *= -1; // Vulkan's Y coordinate is inverted compared to OpenGL's, which glm was designed for originally
        shaderData[0].lightPos = m_LightPosition;
        shaderData[0].lightingEnabled = m_IsLightingEnabled;
        shaderData[0].textureIndex = 0;

        // Terrain
        shaderData[1].model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-20.0f, -15.0f, 30.0f)), glm::vec3(0.2f, 0.2f, 0.2f));
        shaderData[1].view = m_Camera.getViewMatrix();
        shaderData[1].projection = m_Camera.getProjectionMatrix();
        shaderData[1].projection[1][1] *= -1;
        shaderData[1].lightPos = m_LightPosition;
        shaderData[1].lightingEnabled = m_IsLightingEnabled;
        shaderData[1].textureIndex = 1;

        memcpy(m_ShaderDataBuffers[m_FrameIndex].mappedMemory, &shaderData, sizeof(shaderData));
        startTime = currentTime;
    }

    void updateComputePushConstants() {
        m_ComputePushConstants.addressThisFrame = m_ComputeDataBuffers[m_FrameIndex].bufferDeviceAddress;
        if (m_FrameIndex > 0) {
            m_ComputePushConstants.addressLastFrame = m_ComputeDataBuffers[m_FrameIndex - 1].bufferDeviceAddress;
        } else {
            m_ComputePushConstants.addressLastFrame = m_ComputeDataBuffers[MAX_FRAMES_IN_FLIGHT - 1].bufferDeviceAddress;
        }
        m_ComputePushConstants.deltaTime = m_DeltaTime;
    }

    void createInstance() {
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

        constexpr vk::ApplicationInfo appInfo {
            .pApplicationName = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
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
        if (std::ranges::any_of(
            requiredLayers, [&layerProperties](auto const & requiredLayer) {
                return std::ranges::none_of(
                    layerProperties,
                    [requiredLayer](auto const & layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; }
                );
            }
        )) {
            throw std::runtime_error("One or more required layers are not supported!");
        }

        // Get required extensions
        auto requiredExtensions = getRequiredInstanceExtensions();

        // Check if the required extensions are supported by the Vulkan implementation
        auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();

        auto unsupportedPropertyIt =
            std::ranges::find_if(
                requiredExtensions, [&extensionProperties](auto const & requiredExtension) {
                    return std::ranges::none_of(
                        extensionProperties, [requiredExtension](auto const & extensionProperty) {
                            return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
                        }
                    );
                }
            );
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

        // Load function pointers into created instance
#ifdef DISABLE_VULKAN_MODULE
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_Instance);
#else
        vulkanLoader.init(*m_Instance);
#endif
    }

    void setupDebugMessenger() {
        if (!enableValidationLayers) return;

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
        );
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
        );
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
        const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData, void * pUserData
    ) {
        std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

        return vk::False;
    }

    static vk::PFN_VoidFunction loadVulkanFunctionsForImgui(const char * functionName, void * userData) {
        auto * instance = static_cast<vk::Instance *>(userData);
        return instance->getProcAddr(functionName);
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
        auto const devIter = std::ranges::find_if(
            physicalDevices, [&](auto const & physicalDevice) {
                return isDeviceSuitable(physicalDevice);
            }
        );
        if (devIter == physicalDevices.end()) {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }
        m_PhysicalDevice = *devIter;
        m_VulkanResourceService->setPhysicalDevice(m_PhysicalDevice);
        // TODO: ImGui doesn't like multisampling being set here for some reason, find out why
        // m_MsaaSamples = vk::SampleCountFlagBits::e1;
        m_MsaaSamples = getMaxUsableSampleCount();
    }

    bool isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice) {
        bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const & qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

        auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();

        bool supportsAllRequiredExtensions = std::ranges::all_of(
            m_RequiredDeviceExtensions, [&availableDeviceExtensions](auto const & requiredDeviceExtension) {
                return std::ranges::any_of(
                    availableDeviceExtensions, [requiredDeviceExtension](const auto & availableDeviceExtension) {
                        return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
                    }
                );
            }
        );

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
            features.get<vk::PhysicalDeviceVulkan12Features>().runtimeDescriptorArray &&
            features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
            features.get<vk::PhysicalDeviceVulkan12Features>().descriptorIndexing &&
            features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore &&
            features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }

    vk::SampleCountFlagBits getMaxUsableSampleCount() const {
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
            }
        );
        return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
    }

    static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const & availablePresentModes) {
        assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
        return std::ranges::any_of(
                   availablePresentModes, [](const vk::PresentModeKHR value) {
                       return vk::PresentModeKHR::eMailbox == value;
                   }
               )
                   ? vk::PresentModeKHR::eMailbox
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

    void setupImgui() {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO & io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(m_Window, true);

        auto vkSurfaceFormat = static_cast<VkFormat>(m_SwapChainSurfaceFormat.format);
        auto vkDepthFormat = static_cast<VkFormat>(m_VulkanResourceService->findDepthFormat());

        ImGui::StyleColorsDark();

        ImGui_ImplVulkan_InitInfo initInfo = {
            .ApiVersion = vk::ApiVersion14,
            .Instance = *m_Instance,
            .PhysicalDevice = *m_PhysicalDevice,
            .Device = *m_Device,
            .QueueFamily = m_QueueIndex,
            .Queue = *m_GraphicsQueue,
            .DescriptorPoolSize = 8,
            .MinImageCount = 2,
            .ImageCount = 2,
            .PipelineInfoMain = {
                .MSAASamples = static_cast<VkSampleCountFlagBits>(m_MsaaSamples),
                .PipelineRenderingCreateInfo = {
                    .sType = static_cast<VkStructureType>(vk::StructureType::ePipelineRenderingCreateInfoKHR),
                    .colorAttachmentCount = 1,
                    .pColorAttachmentFormats = &vkSurfaceFormat,
                    .depthAttachmentFormat = vkDepthFormat
                },
            },
            .UseDynamicRendering = true,
            .CheckVkResultFn = checkVkResult
        };

        // Need to load Vulkan functions before calling init
        const vk::Instance * instancePtr = &*m_Instance;
        ImGui_ImplVulkan_LoadFunctions(vk::ApiVersion14, loadVulkanFunctionsForImgui, const_cast<vk::Instance *>(instancePtr));

        ImGui_ImplVulkan_Init(&initInfo);
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
        return { m_Device, viewInfo };
    }

    // We are only using this for textures
    void createDescriptorSetLayout() {
        std::array bindings = {
            vk::DescriptorSetLayoutBinding(
                0,
                vk::DescriptorType::eCombinedImageSampler,
                m_ResourceManager.getResourceTypeCount<Texture>(),
                vk::ShaderStageFlagBits::eFragment,
                nullptr
            )
        };

        vk::DescriptorBindingFlags descriptorBindingFlags = {
            vk::DescriptorBindingFlagBits::eVariableDescriptorCount
        };

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
            .bindingCount = 1,
            .pBindingFlags = &descriptorBindingFlags,
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo = {
            .pNext = &bindingFlagsInfo,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };

        m_TextureDescriptorSetLayout = vk::raii::DescriptorSetLayout(m_Device, layoutInfo);
    }

    void createComputePipeline() {
        auto [csShaderModule, csShaderStageInfo] = m_ShaderCompiler.compileAndGetCreateInfo(
            m_Device,
            "shaders/particle_compute_shader.hlsl",
            HlslShaderStage::COMPUTE
        );

        vk::PushConstantRange pushConstantRange = {
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .size = sizeof(ComputePushConstants)
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange
        };
        m_ComputePipelineLayout = m_Device.createPipelineLayout(pipelineLayoutInfo);

        vk::ComputePipelineCreateInfo pipelineCreateInfo = {
            .stage = csShaderStageInfo,
            .layout = m_ComputePipelineLayout
        };

        m_ComputePipeline = m_Device.createComputePipeline(nullptr, pipelineCreateInfo);
    }

    void createGraphicsPipeline() {
        auto [vsShaderModule, vsShaderStageInfo] = m_ShaderCompiler.compileAndGetCreateInfo(
            m_Device,
            "shaders/main_shader.hlsl",
            HlslShaderStage::VERTEX
        );
        auto [psShaderModule, psShaderStageInfo] = m_ShaderCompiler.compileAndGetCreateInfo(
            m_Device,
            "shaders/main_shader.hlsl",
            HlslShaderStage::PIXEL
        );

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };
        vk::PipelineShaderStageCreateInfo shaderStages[] = { vsShaderStageInfo, psShaderStageInfo };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly { .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        // don't need to specify viewports and scissors now as we are going to be setting them dynamically
        vk::PipelineViewportStateCreateInfo viewportState { .viewportCount = 1, .scissorCount = 1 };

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

        vk::PushConstantRange pushConstantRange = {
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .size = sizeof(VertexPushConstants)
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
            .setLayoutCount = 1,                                    // # of descriptor set layouts
            .pSetLayouts = &*m_TextureDescriptorSetLayout,                 // Pointer to descriptor set layouts
            .pushConstantRangeCount = 1,                            // # of push constant ranges
            .pPushConstantRanges = &pushConstantRange               // Pointer to push constant ranges
        };
        m_GraphicsPipelineLayout = vk::raii::PipelineLayout(m_Device, pipelineLayoutInfo);

        vk::Format depthFormat = m_VulkanResourceService->findDepthFormat();
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
            .layout = m_GraphicsPipelineLayout,
            .renderPass = nullptr                   // we're using dynamic rendering instead of render passes
        };

        m_GraphicsPipeline = vk::raii::Pipeline(m_Device, nullptr, pipelineInfo);
    }

    void createPointGraphicsPipeline() {
        auto [vsShaderModule, vsShaderStageInfo] = m_ShaderCompiler.compileAndGetCreateInfo(
            m_Device,
            "shaders/particle_graphics_shader.hlsl",
            HlslShaderStage::VERTEX
        );
        auto [psShaderModule, psShaderStageInfo] = m_ShaderCompiler.compileAndGetCreateInfo(
            m_Device,
            "shaders/particle_graphics_shader.hlsl",
            HlslShaderStage::PIXEL
        );

        auto bindingDescription = Particle::getBindingDescription();
        auto attributeDescriptions = Particle::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };

        vk::PipelineShaderStageCreateInfo shaderStages[] = { vsShaderStageInfo, psShaderStageInfo };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly { .topology = vk::PrimitiveTopology::ePointList };

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState { .viewportCount = 1, .scissorCount = 1 };

        vk::PipelineRasterizationStateCreateInfo rasterizer {
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f
        };

        vk::PipelineMultisampleStateCreateInfo multisampling {
            .rasterizationSamples = m_MsaaSamples,
            .sampleShadingEnable = vk::False
        };

        vk::PipelineDepthStencilStateCreateInfo depthStencil {
            .depthTestEnable = vk::False,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False
        };

        vk::PipelineColorBlendAttachmentState colorBlendAttachment {
            .blendEnable = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        vk::PipelineColorBlendStateCreateInfo colorBlending {
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        vk::PushConstantRange pushConstantRange = {
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .size = sizeof(VertexPushConstants)
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
            .setLayoutCount = 0,                                    // # of descriptor set layouts
            .pSetLayouts = nullptr,                                 // Pointer to descriptor set layouts
            .pushConstantRangeCount = 1,                            // # of push constant ranges
            .pPushConstantRanges = &pushConstantRange               // Pointer to push constant ranges
        };
        m_PointGraphicsPipelineLayout = vk::raii::PipelineLayout(m_Device, pipelineLayoutInfo);

        vk::Format depthFormat = m_VulkanResourceService->findDepthFormat();
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
            .layout = m_GraphicsPipelineLayout,
            .renderPass = nullptr                   // we're using dynamic rendering instead of render passes
        };

        m_PointGraphicsPipeline = vk::raii::Pipeline(m_Device, nullptr, pipelineInfo);
    }

    [[nodiscard]]
    vk::raii::ShaderModule createShaderModule(const std::vector<char> & code) const {
        vk::ShaderModuleCreateInfo createInfo {
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t *>(code.data())
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
        vk::Format depthFormat = m_VulkanResourceService->findDepthFormat();
        VulkanImageData depthResources = m_VulkanResourceService->createDepthResources(
            depthFormat, m_SwapChainExtent, m_MsaaSamples
        );
        m_DepthImage = depthResources.image;
        m_DepthImageMemory = depthResources.imageMemory;
        m_DepthImageView = depthResources.imageView;
    }

    // TODO remove? (unused)
    static bool hasStencilComponent(vk::Format format) {
        return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
    }

    void loadTextures() {
        auto vikingRoomTextureHandle = m_ResourceManager.load<Texture>(VIKING_ROOM_TEXTURE_NAME);
        auto terrainTextureHandle = m_ResourceManager.load<Texture>(TERRAIN_TEXTURE_NAME);
    }

    void loadModels() {
        auto vikingRoomModelHandle = m_ResourceManager.load<Mesh>(VIKING_ROOM_MODEL_NAME);
        auto terrainModelhandle = m_ResourceManager.load<Mesh>(TERRAIN_MODEL_NAME);
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

        vk::SubmitInfo submitInfo { .commandBufferCount = 1, .pCommandBuffers = &*commandBuffer };
        m_GraphicsQueue.submit(submitInfo, nullptr);
        // using a fence instead of waitIdle() would allow us to schedule multiple transfer simultaneously and
        // wait for all of them to complete instead of executing one at a time. ( = likely better optimization)
        m_GraphicsQueue.waitIdle();
    }

    void createShaderDataBuffers() {
        // Count should be equal to number of objects drawn
        // Currently we have 1 object per mesh so this works
        // FIXME change in the future because this will break with more models!
        uint32_t shaderDataObjectCount = m_ResourceManager.getResourceTypeCount<Mesh>();
        m_ShaderDataBuffers = m_VulkanResourceService->createShaderBuffers(
            sizeof(ShaderData) * shaderDataObjectCount, MAX_FRAMES_IN_FLIGHT
        );
    }

    void createComputeBuffers() {
        // Initialize particles
        std::default_random_engine randomnessEngine(static_cast<unsigned>(std::time(nullptr)));
        std::uniform_real_distribution<float> randomDistribution(0.0f, 1.0f);

        // Initial particles positions on a circle
        float pi = 3.14159265358979323846f;
        std::vector<Particle> particles(PARTICLE_COUNT);
        for (auto & particle : particles) {
            float r = 0.25f * std::sqrtf(randomDistribution(randomnessEngine));
            float theta = randomDistribution(randomnessEngine) * 2.0f * pi;
            float x = r * std::cosf(theta) * HEIGHT / WIDTH;
            float y = r * std::sinf(theta);

            particle.position = glm::vec4(x, y, 1.0f, 1.0f);
            particle.velocity = normalize(glm::vec2(x, y)) * 0.00025f;
            particle.color = glm::vec4(
                randomDistribution(randomnessEngine),
                randomDistribution(randomnessEngine),
                randomDistribution(randomnessEngine),
                1.0f
            );
        }

        m_ComputeDataBuffers = m_VulkanResourceService->createComputeBuffers(MAX_FRAMES_IN_FLIGHT, particles);
    }

    void createDescriptorPool() {
        // One texture = one descriptor to allocate
        // Size is important because trying to allocate descriptors beyond the requested count will fail
        std::array poolSize = {
            vk::DescriptorPoolSize(
                vk::DescriptorType::eCombinedImageSampler,
                m_ResourceManager.getResourceTypeCount<Texture>()
            ),
        };
        // Only using descriptors for textures = no need to allocate one set per max frames in flight
        vk::DescriptorPoolCreateInfo poolInfo = {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
            .pPoolSizes = poolSize.data()
        };
        m_DescriptorPool = vk::raii::DescriptorPool(m_Device, poolInfo);
    }

    void createDescriptorSets() {
        uint32_t variableDescCount = m_ResourceManager.getResourceTypeCount<Texture>();
        vk::DescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAllocInfo = {
            .descriptorSetCount = 1,
            .pDescriptorCounts = &variableDescCount
        };
        vk::DescriptorSetAllocateInfo textureDescriptorSetAllocInfo = {
            .pNext = &variableDescCountAllocInfo,
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &*m_TextureDescriptorSetLayout
        };
        m_TextureDescriptorSet.clear();
        m_TextureDescriptorSet = std::move(m_Device.allocateDescriptorSets(textureDescriptorSetAllocInfo).front());

        // For more than one texture, existing textures need to be collected into an array and written from there
        // TODO make something more robust
        std::array<Texture *, 2> textures = {
            m_ResourceManager.getResource<Texture>(VIKING_ROOM_TEXTURE_NAME),
            m_ResourceManager.getResource<Texture>(TERRAIN_TEXTURE_NAME)
        };

        std::vector<vk::DescriptorImageInfo> textureDescriptors {};
        for (auto i = 0; i < variableDescCount; ++i) {
            vk::DescriptorImageInfo imageInfo = {
                .sampler = textures[i]->getSampler(),
                .imageView = textures[i]->getImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            };

            textureDescriptors.push_back(imageInfo);
        }

        vk::WriteDescriptorSet writeDescriptorSet = {
            .dstSet = m_TextureDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = textureDescriptors.data()
        };

        m_Device.updateDescriptorSets(writeDescriptorSet, {});
    }

    void createCommandBuffers() {
        vk::CommandBufferAllocateInfo allocInfo {
            .commandPool = m_CommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };

        m_CommandBuffers = vk::raii::CommandBuffers(m_Device, allocInfo);
    }

    void createComputeCommandBuffers() {
        vk::CommandBufferAllocateInfo allocInfo = {
            .commandPool = m_CommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };

        m_ComputeCommandBuffers = vk::raii::CommandBuffers(m_Device, allocInfo);
    }

    void recordCommandBuffer(uint32_t imageIndex) {
        auto & commandBuffer = m_CommandBuffers[m_FrameIndex];
        commandBuffer.reset();
        commandBuffer.begin({});

        // Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        m_VulkanResourceService->transitionImageLayout(
            *commandBuffer,
            m_SwapChainImages[imageIndex],
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal
        );
        // Transition multisampled image to COLOR_ATTACHMENT_OPTIMAL
        m_VulkanResourceService->transitionImageLayout(
            *commandBuffer,
            m_ColorImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal
        );
        // Transition depth image to DEPTH_ATTACHMENT_OPTIMAL
        m_VulkanResourceService->transitionImageLayout(
            *commandBuffer,
            m_DepthImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal
        );

        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor
        };

        if (m_MsaaSamples == vk::SampleCountFlagBits::e1) {
            colorAttachmentInfo.imageView = m_SwapChainImageViews[imageIndex];
        } else {
            // Multisampled color attachment with resolve attachment
            // According to spec, if resolveMode is not RESOLVE_MODE_NONE, and resolveImageView is not null,
            // a render pass multisample resolve operation is defined for the attachment subresource.
            colorAttachmentInfo.imageView = m_ColorImageView;
            colorAttachmentInfo.resolveMode = vk::ResolveModeFlagBits::eAverage;
            colorAttachmentInfo.resolveImageView = m_SwapChainImageViews[imageIndex];
        }

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

        // Set common command buffer values
        commandBuffer.setViewport(
            0, vk::Viewport(
                0.0f, 0.0f,
                static_cast<float>(m_SwapChainExtent.width), static_cast<float>(m_SwapChainExtent.height),
                0.0f, 1.0f
            )
        );
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapChainExtent));

        VertexPushConstants vertexPushConstants = {
            .shaderDataStartAddress = m_ComputeDataBuffers[m_FrameIndex].bufferDeviceAddress,
            .shaderDataIndex = 0,
            .particlesEnabled = m_ParticlesEnabled,
        };

        // Draw particles
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_PointGraphicsPipeline);
        commandBuffer.bindVertexBuffers(0, m_ComputeDataBuffers[m_FrameIndex].buffer, { 0 });
        commandBuffer.pushConstants(
            m_PointGraphicsPipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(VertexPushConstants),
            &vertexPushConstants
        );
        commandBuffer.draw(PARTICLE_COUNT, 1, 0, 0);

        // Common model rendering bindings
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_GraphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_GraphicsPipelineLayout, 0, *m_TextureDescriptorSet, nullptr);

        commandBuffer.pushConstants(
            m_GraphicsPipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(VertexPushConstants),
            &vertexPushConstants
        );

        // Draw viking room mesh
        const auto vikingRoomMesh = m_ResourceManager.getResource<Mesh>(VIKING_ROOM_MODEL_NAME);

        commandBuffer.bindVertexBuffers(0, vikingRoomMesh->getVertexBuffer(), { 0 });
        commandBuffer.bindIndexBuffer(vikingRoomMesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        vertexPushConstants.shaderDataStartAddress = m_ShaderDataBuffers[m_FrameIndex].bufferDeviceAddress;
        commandBuffer.pushConstants(
            m_GraphicsPipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(VertexPushConstants),
            &vertexPushConstants
        );

        commandBuffer.drawIndexed(vikingRoomMesh->getIndexCount(), 1, 0, 0, 0);

        // Draw terrain mesh
        const auto terrainMesh = m_ResourceManager.getResource<Mesh>(TERRAIN_MODEL_NAME);
        commandBuffer.bindVertexBuffers(0, terrainMesh->getVertexBuffer(), { 0 });
        commandBuffer.bindIndexBuffer(terrainMesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
        vertexPushConstants.shaderDataIndex = 1;

        commandBuffer.pushConstants(
            m_GraphicsPipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(VertexPushConstants),
            &vertexPushConstants
        );

        commandBuffer.drawIndexed(terrainMesh->getIndexCount(), 1, 0, 0, 0);

        // Draw ImGui
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *m_CommandBuffers[m_FrameIndex]);
        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to PRESENT_SRC

        m_VulkanResourceService->transitionImageLayout(
            *commandBuffer,
            m_SwapChainImages[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR
        );

        commandBuffer.end();
    }

    void recordComputeCommandBuffer() {
        auto & commandBuffer = m_ComputeCommandBuffers[m_FrameIndex];
        commandBuffer.reset();

        commandBuffer.reset();
        commandBuffer.begin({});
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_ComputePipeline);
        commandBuffer.pushConstants(
            m_ComputePipelineLayout,
            vk::ShaderStageFlagBits::eCompute,
            0,
            sizeof(ComputePushConstants),
            &m_ComputePushConstants
        );
        // Correlates to [numthreads(x, y, z)] inside the shader
        commandBuffer.dispatch(PARTICLE_COUNT / 256, 1, 1);
        commandBuffer.end();
    }

    void createSyncObjects() {
        assert(m_InFlightFences.empty());

        vk::SemaphoreTypeCreateInfo semaphoreType = {
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0
        };
        m_Semaphore = m_Device.createSemaphore({ .pNext = &semaphoreType });

        for (size_t i = 0; i < m_SwapChainImages.size(); ++i) {
            m_InFlightFences.emplace_back(m_Device, vk::FenceCreateInfo {});
        }
    }

    void cleanupSwapChain() {
        m_SwapChainImageViews.clear();
        m_SwapChain = nullptr; // doing this automatically calls the destructor which in turn calls swapChain.clear()
    }

    void cleanupColorResources(const vk::Device & device) const {
        device.destroyImageView(m_ColorImageView);
        device.destroyImage(m_ColorImage);
        device.freeMemory(m_ColorImageMemory);
    }

    void cleanupDepthResources(const vk::Device & device) const {
        device.destroyImageView(m_DepthImageView);
        device.destroyImage(m_DepthImage);
        device.freeMemory(m_DepthImageMemory);
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

        const auto & deviceHandle = *m_Device;

        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        cleanupColorResources(deviceHandle);
        createColorResources();
        cleanupDepthResources(deviceHandle);
        createDepthResources();
    }

    void recompileShadersAndRecreatePipeline() {
        m_Device.waitIdle();

        m_ComputePipeline.clear();
        m_GraphicsPipeline.clear();
        m_PointGraphicsPipeline.clear();

        createComputePipeline();
        createGraphicsPipeline();
        createPointGraphicsPipeline();
        m_ShadersSetToReload = false;
    }

private:
    std::shared_ptr<VulkanResourceService> m_VulkanResourceService = nullptr;
    ResourceManager m_ResourceManager {};
    HlslShaderCompiler m_ShaderCompiler {};

    GLFWwindow * m_Window = nullptr;

    Camera m_Camera {};

    float m_DeltaTime = 0.0f;
    float m_FrameTimeMs = 0.0f;
    uint32_t m_Fps = 0;

    // Dynamic shader data
    bool m_IsLightingEnabled = true;
    bool m_ParticlesEnabled = true;
    glm::vec4 m_LightPosition = { 0.0f, -10.0f, 10.0f, 0.0f };

    bool m_IsWindowFocused = true;
    bool m_ShouldCursorBeVisible = false;
    bool m_CanUiFocusBeToggled = false;     // Seems janky, should probably move to a more robust input state handling method later
    bool m_IsModelSpinEnabled = true;

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

    vk::raii::PipelineLayout m_ComputePipelineLayout = nullptr;
    vk::raii::Pipeline m_ComputePipeline = nullptr;

    vk::raii::DescriptorSetLayout m_TextureDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout m_GraphicsPipelineLayout = nullptr;
    vk::raii::Pipeline m_GraphicsPipeline = nullptr;

    vk::raii::PipelineLayout m_PointGraphicsPipelineLayout = nullptr;
    vk::raii::Pipeline m_PointGraphicsPipeline = nullptr;

    bool m_ShadersSetToReload = false;

    vk::raii::CommandPool m_CommandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> m_CommandBuffers {};
    std::vector<vk::raii::CommandBuffer> m_ComputeCommandBuffers {};

    vk::raii::Semaphore m_Semaphore = nullptr;
    uint64_t m_TimelineValue = 0;
    std::vector<vk::raii::Fence> m_InFlightFences {};
    uint32_t m_FrameIndex = 0;

    // Note: the below setup is far from optimal; ideally we should allocate a single buffer
    // and store both our vertex and index data in there, rather than creating individual buffers.
    // It's also good to use a memory allocator on top of it all, like VMA. We are not doing it
    // here because it would be slightly overkill for drawing just a couple simple shapes.
    // We'll optimize this once there's an actual need/reason to do so.

    std::vector<VulkanShaderBufferData> m_ShaderDataBuffers {};
    std::vector<VulkanComputeBufferData> m_ComputeDataBuffers {};
    ComputePushConstants m_ComputePushConstants {};

    vk::raii::DescriptorPool m_DescriptorPool = nullptr;
    vk::raii::DescriptorSet m_TextureDescriptorSet = nullptr;

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
