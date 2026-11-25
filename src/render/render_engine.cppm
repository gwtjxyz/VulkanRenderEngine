module;

#include <cassert>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

#ifdef DISABLE_IMPORT_STD
#include <iostream>
#include <random>
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

import asset_manager;
import camera;
import constants;
import components;
import entity_system;
import descriptor_manager;
import device_mapper;
import draw_executor;
import model_parser;
import pipeline_manager;
import platform;
import render_types;
import texture_manager;
import vulkan_instance;
import vulkan_resource_service;

import glm;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif // NDEBUG

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

static void checkVkResult(VkResult err) {
    if (err == 0)
        return;
    std::cerr << "[vulkan] Error: VkResult = " << err << std::endl;
    // TODO: don't know if we really want to crash on any Vulkan error inside ImGui
    if (err < 0)
        std::abort();
}

export class RenderEngine {
public:
    void run(const std::string & appName, const int mazeWidth, const int mazeHeight) {
        if (mazeWidth > 5) {
            m_MazeWidth = mazeWidth;
        } else {
            m_MazeWidth = MAZE_WIDTH;
        }

        if (mazeHeight > 5) {
            m_MazeHeight = mazeHeight;
        } else {
            m_MazeHeight = MAZE_HEIGHT;
        }

        initServices();
        initWindow(appName);
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    void initServices() {
        m_VulkanResourceService = std::make_shared<VulkanResourceService>();
        VulkanResourceServiceLocator::provide(m_VulkanResourceService.get());
        m_DescriptorManager = std::make_shared<DescriptorManager>();
        DescriptorManagerLocator::provide(m_DescriptorManager.get());
        m_DrawExecutor = std::make_shared<DrawExecutor>();
        DrawExecutorLocator::provide(m_DrawExecutor.get());
        m_PipelineManager = std::make_shared<PipelineManager>();
        PipelineManagerLocator::provide(m_PipelineManager.get());
        m_DeviceMapper = std::make_shared<DeviceMapper>();
        DeviceMapperLocator::provide(m_DeviceMapper.get());
        m_ModelParser = std::make_shared<ModelParser>();
        ModelParserLocator::provide(m_ModelParser.get());
        m_TextureManager = std::make_shared<TextureManager>();
        TextureManagerLocator::provide(m_TextureManager.get());
    }

    void initWindow(const std::string & appName) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(WIDTH, HEIGHT, appName.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, framebufferResizeCallback);
        glfwSetKeyCallback(m_Window, keyCallback);
        glfwSetMouseButtonCallback(m_Window, mouseButtonCallback);
        glfwSetWindowFocusCallback(m_Window, windowFocusCallback);
        glfwSetCursorPosCallback(m_Window, mouseCallback);
        glfwSetScrollCallback(m_Window, scrollCallback);

        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    static void framebufferResizeCallback(GLFWwindow * window, [[maybe_unused]] int width, [[maybe_unused]] int height) {
        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
        app->m_FramebufferResized = true;
    }

    static void keyCallback(GLFWwindow * window, int key, [[maybe_unused]] int scancode, int action, [[maybe_unused]] int mods) {
        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
        if (key == GLFW_KEY_R && action == GLFW_PRESS) {
            app->m_ShadersSetToReload = true;
        }
        if (key == GLFW_KEY_G && action == GLFW_PRESS) {
            app->m_Camera.toggleGravity();
        }
        if (key == GLFW_KEY_M && action == GLFW_PRESS) {
            app->m_DrawMinimap = !app->m_DrawMinimap;
        }
    }

    static void mouseButtonCallback(GLFWwindow * window, int button, int action, [[maybe_unused]] int mods) {
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
        if (isImguiCapturingMouse() || app->m_ShouldCursorBeVisible) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        app->m_Camera.processMouseMovement(xOffset, yOffset);
    }

    static void scrollCallback(GLFWwindow * window, [[maybe_unused]] double xOffset, double yOffset) {
        ImGuiIO & io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }

        auto app = static_cast<RenderEngine *>(glfwGetWindowUserPointer(window));
        app->m_Camera.processMouseScroll(static_cast<float>(yOffset));
    }

    void initVulkan() {
        // Init core Vulkan stuff
        m_Instance.initialize(vk::ApiVersion14, enableValidationLayers, m_Window);
        m_VulkanResourceService->setVulkanInstance(&m_Instance);
        m_PipelineManager->setVulkanInstance(&m_Instance);

        // MSAA (TODO: allow this to be adjusted on the fly?)
        m_MsaaSamples = getMaxUsableSampleCount();

        // Swapchain
        createSwapChain();

        // GUI
        setupImgui();

        // Pipeline draw data
        m_DeviceMapper->preallocateBuffers(100, MAX_FRAMES_IN_FLIGHT);

        // Descriptors
        m_DescriptorManager->setVulkanInstance(&m_Instance);
        m_DescriptorManager->initialize();

        // Load assets
        m_TextureManager->initialize();
        loadEntities();

        // Indirect draw executor
        m_DrawExecutor->initialize();

        // Pipeline layout setup
        m_PipelineManager->createComputePipeline();
        m_PipelineManager->createGraphicsPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);
        m_PipelineManager->createPointGraphicsPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);
        m_PipelineManager->createMinimapPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);

        // Command buffers
        createCommandBuffers();
        // Particle data
        initComputeParticleBuffers();

        // Image resources to draw into
        createImageResources();

        // Synchronization setup
        createSyncObjects();
    }

    void mainLoop() {
        // Call this once before the loop starts to initialize delta time
        advanceDeltaTime();

        uint32_t frameCount = 0;
        while (!glfwWindowShouldClose(m_Window)) {
            auto timeAtFrameStart = std::chrono::high_resolution_clock::now();
            static auto timeSinceLastFpsMeasurement = timeAtFrameStart;

            advanceDeltaTime();

            glfwPollEvents();
            processContinuousInput(m_Window, m_Camera, m_DeltaTime);

            if (m_ShadersSetToReload) {
                recompileShadersAndRecreatePipeline();
            }

            updateEntities();

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
    }

    static bool isImguiCapturingKeyboard() {
        const ImGuiIO & io = ImGui::GetIO();

        return io.WantCaptureKeyboard;
    }

    static bool isImguiCapturingMouse() {
        const ImGuiIO & io = ImGui::GetIO();

        return io.WantCaptureMouse;
    }

    // For continuously inputting keys; for keys only being pressed once, we keep the callback
    void processContinuousInput(GLFWwindow * window, Camera & camera, const float deltaTime) {
        if (isImguiCapturingKeyboard())
            return;

        bool sprinting = false;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            sprinting = true;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::FORWARD, deltaTime, sprinting, m_EntitySystem);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::BACKWARD, deltaTime, sprinting, m_EntitySystem);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::LEFT, deltaTime, sprinting, m_EntitySystem);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::RIGHT, deltaTime, sprinting, m_EntitySystem);

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::UP, deltaTime, sprinting, m_EntitySystem);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            camera.processKeyboard(CameraMovement::DOWN, deltaTime, sprinting, m_EntitySystem);

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

        cleanupImageResources();

        for (auto & computeDataBuffer : m_ComputeDataBuffers) {
            m_VulkanResourceService->freeResources(computeDataBuffer.buffer, computeDataBuffer.bufferMemory);
        }

        m_VulkanResourceService->freeResources(m_MinimapBuffer.buffer, m_MinimapBuffer.bufferMemory);

        m_DescriptorManager->destroy();
        m_TextureManager->destroyAll();
        m_AssetManager.destroyAll();
        m_DrawExecutor->destroy();
        m_PipelineManager->destroyAll();

        m_DeviceMapper->freeResources();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void drawFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        auto [result, imageIndex] = m_SwapChain.acquireNextImage(UINT64_MAX, nullptr, *m_InFlightFences[m_FrameIndex]);
        auto fenceResult = m_Instance.getDevice().waitForFences(*m_InFlightFences[m_FrameIndex], vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for fence!");
        }

        m_Instance.getDevice().resetFences(*m_InFlightFences[m_FrameIndex]);

        // Update timeline value for this frame
        uint64_t computeWaitValue = m_TimelineValue;
        uint64_t computeSignalValue = ++m_TimelineValue;
        uint64_t graphicsWaitValue = computeSignalValue;
        uint64_t graphicsSignalValue = ++m_TimelineValue;

        // Compute
        {
            updateComputePushConstants();
            // Need to record and submit work even if we're not actually displaying anything
            // to signal timeline semaphores, otherwise subsequent stages will wait forever
            recordComputeCommandBuffer();

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

            // TODO use separate queue for compute
            m_Instance.getGraphicsQueue().submit(computeSubmitInfo, nullptr);
        }

        // Graphics
        {
            recordGraphicsCommandBuffer(imageIndex);

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
                .pCommandBuffers = &*m_GraphicsCommandBuffers[m_FrameIndex],
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &*m_Semaphore
            };

            m_Instance.getGraphicsQueue().submit(graphicsSubmitInfo, nullptr);
        }

        // Present
        {
            // Present the image (wait for graphics to finish)
            vk::SemaphoreWaitInfo waitInfo = {
                .semaphoreCount = 1,
                .pSemaphores = &*m_Semaphore,
                .pValues = &graphicsSignalValue
            };
            // Wait for graphics to complete before presenting
            result = m_Instance.getDevice().waitSemaphores(waitInfo, UINT64_MAX);
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

            result = m_Instance.getGraphicsQueue().presentKHR(presentInfoKHR);
            if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || m_FramebufferResized) {
                m_FramebufferResized = false;
                recreateSwapChain();
            } else {
                // There are no other success codes other than eSuccess; on any error code, presentKHR already threw an exception.
                assert(result == vk::Result::eSuccess);
            }
        }

        // Advance frame index
        m_FrameIndex = (m_FrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // In the future will probably extract it to somewhere else (possibly a separate class/system?)
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
        ImGui::Text("G to toggle gravity, M to toggle minimap");
        ImGui::Spacing();
        ImGui::Text("To change the maze size, change the");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
        ImGui::Text("'MAZE_WIDTH' and 'MAZE_HEIGHT'");
        ImGui::PopStyleColor();
        ImGui::Text("parameters inside constants.cppm, or supply the width");
        ImGui::Text("and height via command line arguments.");
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Shader Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char * lightingModeLabel;
            switch (m_LightMode) {
                case LightMode::Off:
                    lightingModeLabel = "Off";
                    break;
                case LightMode::Phong:
                    lightingModeLabel = "Phong";
                    break;
                case LightMode::Gooch:
                    lightingModeLabel = "Gooch";
                    break;
                default:
                    lightingModeLabel = "Unknown";
                    break;
            }

            auto lightModeInt = static_cast<int32_t>(m_LightMode);
            ImGui::SliderInt("Lighting mode", &lightModeInt, 0, 2, lightingModeLabel);
            m_LightMode = static_cast<LightMode>(lightModeInt);

            if (ImGui::BeginTable("ShaderControlCheckboxes", 2)) {
                ImGui::TableNextColumn();
                ImGui::Checkbox("Particles enabled", &m_ParticlesEnabled);
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();

        // Object picker dropdown
        if (ImGui::CollapsingHeader("Scene Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            static int selectedObject = -1;
            static int previouslySelectedObject = selectedObject;

            if (m_ObjectNamesOutOfDate) {
                m_ObjectNames.clear();

                const auto & objectStrings = m_EntitySystem.getObjectNames();
                for (const auto & s : objectStrings) {
                    m_ObjectNames.push_back(s.c_str());
                }
                m_ObjectNamesOutOfDate = false;
            }

            ImGui::Combo("Object", &selectedObject, m_ObjectNames.data(), static_cast<int>(m_ObjectNames.size()));
            if (selectedObject != -1) {
                // Need to display controls for changing object's transform
                auto objectName = m_ObjectNames.at(selectedObject);
                auto transform = m_EntitySystem.getTransform(objectName);
                assert(transform);

                static float transformPosWidgetData[3] {};
                static glm::vec3 rotationAngles;
                static float transformRotWidgetData[3] {};
                static float transformScaleWidgetData {};
                if (previouslySelectedObject != selectedObject) {
                    transformPosWidgetData[0] = transform->position.x;
                    transformPosWidgetData[1] = transform->position.y;
                    transformPosWidgetData[2] = transform->position.z;

                    rotationAngles = glm::degrees(glm::eulerAngles(transform->rotation));

                    transformRotWidgetData[0] = rotationAngles.x;
                    transformRotWidgetData[1] = rotationAngles.y;
                    transformRotWidgetData[2] = rotationAngles.z;

                    transformScaleWidgetData = transform->scale.x;

                    previouslySelectedObject = selectedObject;
                }

                ImGui::DragFloat3("Object position", transformPosWidgetData, 0.005f, -1000.0f, 1000.0f, "%.3f");
                ImGui::DragFloat3("Object rotation", transformRotWidgetData, 0.1f, -360.0f, 360.0f, "%.1f");
                ImGui::DragFloat("Object scale", &transformScaleWidgetData, 0.005f, 0.0f, 1000.0f, "%.3f");

                constexpr float tolerance = 0.001f;
                if (isDifferentEnough(transform->position.x, transformPosWidgetData[0], tolerance) ||
                    isDifferentEnough(transform->position.y, transformPosWidgetData[1], tolerance) ||
                    isDifferentEnough(transform->position.z, transformPosWidgetData[2], tolerance)) {
                    m_EntitySystem.setPosition(objectName, { transformPosWidgetData[0], transformPosWidgetData[1], transformPosWidgetData[2] });
                }

                const float pitch = glm::degrees(glm::pitch(transform->rotation));
                const float yaw = glm::degrees(glm::yaw(transform->rotation));
                const float roll = glm::degrees(glm::roll(transform->rotation));

                if (isDifferentEnough(pitch, transformRotWidgetData[0], tolerance) ||
                    isDifferentEnough(yaw, transformRotWidgetData[1], tolerance) ||
                    isDifferentEnough(roll, transformRotWidgetData[2], tolerance)) {
                    m_EntitySystem.setRotation(objectName, transformRotWidgetData[0], transformRotWidgetData[1], transformRotWidgetData[2]);
                }

                if (isDifferentEnough(transform->scale.x, transformScaleWidgetData, tolerance)) {
                    m_EntitySystem.setScale(objectName, transformScaleWidgetData);
                }
            }
        }

        if (ImGui::CollapsingHeader("Light Controls (WIP)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            static int selectedLight = -1;
            static int previouslySelectedLight = selectedLight;
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x + 5.0f;

            static auto lightCount = m_EntitySystem.getLightNames().size();

            ImGui::Text("Light Count: %llu", lightCount);
            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button("Add")) {
                m_EntitySystem.createLight(m_AssetManager);
                selectedLight = static_cast<int>(m_EntitySystem.getLightNames().size() - 1);
                lightCount++;

                m_LightNamesOutOfDate = true;
            }
            ImGui::SameLine(0.0f, spacing);
            ImGui::Checkbox("Draw light cubes", &m_DrawLightCubes);

            if (m_LightNamesOutOfDate) {
                m_LightNames.clear();

                const auto & lightStrings = m_EntitySystem.getLightNames();
                for (const auto & s : lightStrings) {
                    m_LightNames.push_back(s.c_str());
                }
                m_LightNamesOutOfDate = false;
            }

            ImGui::Combo("Light", &selectedLight, m_LightNames.data(), static_cast<int>(m_LightNames.size()));
            if (selectedLight != -1) {
                // Light properties

                const auto * lightName = m_LightNames.at(selectedLight);
                const auto * lightComponent = m_EntitySystem.getLight(lightName);
                assert(lightComponent);
                const auto * transformComponent = m_EntitySystem.getTransform(lightName);
                assert(transformComponent);

                static float lightPos[3];
                static glm::vec3 lightDirAngles;
                static float lightDirection[3];
                static float lightColor[4];
                static int32_t lightType = 0;
                if (previouslySelectedLight != selectedLight) {
                    lightPos[0] = transformComponent->position.x;
                    lightPos[1] = transformComponent->position.y;
                    lightPos[2] = transformComponent->position.z;

                    lightDirAngles = glm::degrees(glm::eulerAngles(transformComponent->rotation));

                    lightDirection[0] = lightDirAngles.x;
                    lightDirection[1] = lightDirAngles.y;
                    lightDirection[2] = lightDirAngles.z;

                    lightColor[0] = lightComponent->color.r;
                    lightColor[1] = lightComponent->color.g;
                    lightColor[2] = lightComponent->color.b;
                    lightColor[3] = lightComponent->intensity;

                    lightType = static_cast<uint32_t>(lightComponent->type);

                    previouslySelectedLight = selectedLight;
                }

                constexpr float tolerance = 0.001f;
                if (isDifferentEnough(transformComponent->position.x, lightPos[0], tolerance) ||
                    isDifferentEnough(transformComponent->position.y, lightPos[1], tolerance) ||
                    isDifferentEnough(transformComponent->position.z, lightPos[2], tolerance)) {
                    m_EntitySystem.setPosition(lightName, { lightPos[0], lightPos[1], lightPos[2] });
                }

                const float pitch = glm::degrees(glm::pitch(transformComponent->rotation));
                const float yaw = glm::degrees(glm::yaw(transformComponent->rotation));
                const float roll = glm::degrees(glm::roll(transformComponent->rotation));

                if (isDifferentEnough(pitch, lightDirection[0], tolerance) ||
                    isDifferentEnough(yaw, lightDirection[1], tolerance) ||
                    isDifferentEnough(roll, lightDirection[2], tolerance)) {
                    m_EntitySystem.setRotation(lightName, lightDirection[0], lightDirection[1], lightDirection[2]);
                }

                if (isDifferentEnough(lightComponent->color.r, lightColor[0], tolerance) ||
                    isDifferentEnough(lightComponent->color.g, lightColor[1], tolerance) ||
                    isDifferentEnough(lightComponent->color.b, lightColor[2], tolerance)) {
                    m_EntitySystem.setColor(lightName, lightColor); // Will this work?
                }

                if (isDifferentEnough(lightComponent->intensity, lightColor[3], tolerance)) {
                    m_EntitySystem.setIntensity(lightName, lightColor[3]);
                }

                if (lightType != static_cast<int32_t>(lightComponent->type)) {
                    m_EntitySystem.setLightType(lightName, static_cast<Components::LightType>(lightType));
                }

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                ImGui::DragFloat3("Position", lightPos, 0.005f, -20.0f, 20.0f, "%.3f");
                ImGui::DragFloat3("Direction", lightDirection, 0.5f, -360.0f, 360.0f, "%.1f");

                ImGui::ColorEdit4("Light Color", lightColor);

                const char * lightTypeLabel;
                switch (static_cast<Components::LightType>(lightType)) {
                    case Components::LightType::Directional:
                        lightTypeLabel = "Directional";
                        break;
                    case Components::LightType::Point:
                        lightTypeLabel = "Point";
                        break;
                    case Components::LightType::Spotlight:
                        lightTypeLabel = "Spotlight";
                        break;
                    default:
                        lightTypeLabel = "Unknown";
                        break;
                }

                // TODO enabled once we support more light types
                ImGui::BeginDisabled();
                ImGui::SliderInt("Light Type", &lightType, 0, 2, lightTypeLabel);
                ImGui::EndDisabled();

                if (ImGui::Button("Delete Light")) {
                    m_EntitySystem.removeLight(selectedLight);
                    m_LightNamesOutOfDate = true;
                    selectedLight = -1;
                    lightCount--;
                }

            }
        }

        ImGui::PopItemWidth();

        ImGui::End();
    }

    // TODO probably extract to somewhere
    inline static bool isDifferentEnough(const float a, const float b, const float tolerance) {
        return std::fabsf(a - b) >= tolerance;
    }

    void advanceDeltaTime() {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        m_DeltaTime = std::chrono::duration<float>(currentTime - startTime).count();
        startTime = currentTime;
    }

    void updateEntities() {
        glm::vec3 exitPosition = m_EntitySystem.getExitPosition();
        glm::vec3 cameraPosition = m_Camera.getPosition();

        // Display compute particle ring upon reaching the exit
        if (cameraPosition.x > exitPosition.x && cameraPosition.z > exitPosition.z &&
            cameraPosition.x < exitPosition.x + 2.0f && cameraPosition.z < cameraPosition.z + 2.0f) {
            m_ParticlesEnabled = true;
        }
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

    static vk::PFN_VoidFunction loadVulkanFunctionsForImgui(const char * functionName, void * userData) {
        auto * instance = static_cast<vk::Instance *>(userData);
        return instance->getProcAddr(functionName);
    }

    vk::SampleCountFlagBits getMaxUsableSampleCount() const {
        vk::PhysicalDeviceProperties physicalDeviceProperties = m_Instance.getPhysicalDeviceProperties();

        vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
        if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
        if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
        if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
        if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
        if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

        return vk::SampleCountFlagBits::e1;
    }

    void createSwapChain() {
        const auto physicalDevice = m_Instance.getPhysicalDevice();
        const auto surface = m_Instance.getSurface();
        const vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
        m_SwapChainExtent = chooseSwapExtent(surfaceCapabilities);
        const uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(surface);
        m_SwapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

        std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(surface);

        vk::SwapchainCreateInfoKHR swapChainCreateInfo = {
            .surface = surface,
            .minImageCount = minImageCount,
            .imageFormat = m_SwapChainSurfaceFormat.format,
            .imageColorSpace = m_SwapChainSurfaceFormat.colorSpace,
            .imageExtent = m_SwapChainExtent,
            .imageArrayLayers = 1,                                          // always 1 unless developing stereoscopic 3D app
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,         // rendering directly to swap chain images; can use eTransferDst if we want to do post-processing first
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = chooseSwapPresentMode(availablePresentModes),
            .clipped = true
        };

        m_SwapChain = vk::raii::SwapchainKHR(m_Instance.getRaiiDevice(), swapChainCreateInfo);
        m_SwapChainImages = m_SwapChain.getImages();

        assert(m_SwapChainImageViews.empty());

        for (auto & image : m_SwapChainImages) {
            m_SwapChainImageViews.emplace_back(
                m_VulkanResourceService->createImageView(
                    image,
                    m_SwapChainSurfaceFormat.format,
                    vk::ImageAspectFlagBits::eColor,
                    1
                )
            );
        }

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

    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const & capabilities) const {
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
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(m_Window, true);

        ImGui::StyleColorsDark();
        auto & style = ImGui::GetStyle();
        style.Alpha = 1.0f;
        style.WindowRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.TabRounding = 3.0f;

        auto vkSurfaceFormat = static_cast<VkFormat>(m_SwapChainSurfaceFormat.format);
        auto vkDepthFormat = static_cast<VkFormat>(m_VulkanResourceService->findDepthFormat());

        ImGui::StyleColorsDark();
        ImGui_ImplVulkan_InitInfo initInfo = {
            .ApiVersion = vk::ApiVersion14,
            .Instance = m_Instance.getInstance(),
            .PhysicalDevice = m_Instance.getPhysicalDevice(),
            .Device = m_Instance.getDevice(),
            .QueueFamily = m_Instance.getQueueIndex(),
            .Queue = m_Instance.getGraphicsQueue(),
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
        auto instance = m_Instance.getInstance();
        auto instancePtr = &instance;
        ImGui_ImplVulkan_LoadFunctions(vk::ApiVersion14, loadVulkanFunctionsForImgui, instancePtr);

        ImGui_ImplVulkan_Init(&initInfo);
    }

    void createImageResources() {
        VulkanImageData colorResources = m_VulkanResourceService->createColorResources(
            m_SwapChainSurfaceFormat.format, m_SwapChainExtent, m_MsaaSamples
        );
        m_ColorImage = colorResources.image;
        m_ColorImageMemory = colorResources.imageMemory;
        m_ColorImageView = colorResources.imageView;

        vk::Format depthFormat = m_VulkanResourceService->findDepthFormat();
        VulkanImageData depthResources = m_VulkanResourceService->createDepthResources(
            depthFormat, m_SwapChainExtent, m_MsaaSamples
        );
        m_DepthImage = depthResources.image;
        m_DepthImageMemory = depthResources.imageMemory;
        m_DepthImageView = depthResources.imageView;
    }

    void loadEntities() {
        m_EntitySystem.setupWorld(m_AssetManager, m_MazeWidth, m_MazeHeight);
        auto & minimap = m_EntitySystem.getMinimapDisplay();
        m_MinimapBuffer = m_VulkanResourceService->createVulkanBuffer(
            minimap.size() * sizeof(Particle),
            vk::BufferUsageFlagBits::eVertexBuffer,
            minimap.data()
        );
        m_MinimapSize = static_cast<uint32_t>(minimap.size());
        m_AssetManager.loadAll();
        m_Camera.setPosition(m_EntitySystem.getStartPosition());

        m_DescriptorManager->writeToDescriptors();
    }

    void initComputeParticleBuffers() {
        // Initialize particles
        std::default_random_engine randomnessEngine(static_cast<unsigned>(std::time(nullptr)));
        std::uniform_real_distribution<float> randomDistribution(0.0f, 1.0f);

        // Initial particles positions on a circle
        // TODO extract pi to somewhere else
        const float pi = 3.14159265358979323846f;
        std::vector<Particle> particles(PARTICLE_COUNT);
        for (auto & particle : particles) {
            const float r = 0.25f * std::sqrtf(randomDistribution(randomnessEngine));
            const float theta = randomDistribution(randomnessEngine) * 2.0f * pi;
            const float x = r * std::cosf(theta) * HEIGHT / WIDTH;
            const float y = r * std::sinf(theta);

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

    void createCommandBuffers() {
        // Compute
        {
            vk::CommandBufferAllocateInfo allocInfo {
                .commandPool = m_Instance.getCommandPool(),
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = MAX_FRAMES_IN_FLIGHT
            };

            m_GraphicsCommandBuffers = vk::raii::CommandBuffers(m_Instance.getRaiiDevice(), allocInfo);
        }

        // Graphics
        {
            vk::CommandBufferAllocateInfo allocInfo = {
                .commandPool = m_Instance.getCommandPool(),
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = MAX_FRAMES_IN_FLIGHT
            };

            m_ComputeCommandBuffers = vk::raii::CommandBuffers(m_Instance.getRaiiDevice(), allocInfo);
        }
    }

    void recordGraphicsCommandBuffer(const uint32_t imageIndex) const {
        auto & commandBuffer = m_GraphicsCommandBuffers[m_FrameIndex];
        commandBuffer.reset();
        commandBuffer.begin({});

        m_PipelineManager->setActiveCommandBuffer(commandBuffer);

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

        const vk::RenderingInfo renderingInfo = {
            .renderArea = { .offset = { .x = 0, .y = 0 }, .extent = m_SwapChainExtent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo
        };

        // Set common command buffer values
        commandBuffer.setViewport(
            0, vk::Viewport(
                0.0f, 0.0f,
                static_cast<float>(m_SwapChainExtent.width), static_cast<float>(m_SwapChainExtent.height),
                0.0f, 1.0f
            )
        );
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapChainExtent));

        commandBuffer.beginRendering(renderingInfo);

        // Set various push constants
        m_PipelineManager->setCameraVariables(m_Camera.getPosition(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix());
        m_PipelineManager->prepareAddressesForFrame(m_FrameIndex);
        m_PipelineManager->setParticlesEnabled(m_ParticlesEnabled);

        // Common object rendering bindings
        m_PipelineManager->setAndBindActivePipeline(GRAPHICS_PIPELINE_NAME);
        m_PipelineManager->setLightMode(m_LightMode);
        m_PipelineManager->setLightCount(m_DeviceMapper->getLightCount());

        // Draw entities
        m_EntitySystem.prepareAndRender(m_FrameIndex, m_DrawLightCubes);

        // Draw minimap
        if (m_DrawMinimap) {
            m_PipelineManager->setAndBindActivePipeline(MINIMAP_PIPELINE_NAME);
            m_PipelineManager->simpleDraw(m_MinimapBuffer.buffer, m_MinimapSize, 1);
        }

        // Draw particles
        if (m_ParticlesEnabled) {
            m_PipelineManager->setAndBindActivePipeline(POINT_GRAPHICS_PIPELINE_NAME);
            m_PipelineManager->bindAndDraw(m_ComputeDataBuffers[m_FrameIndex].buffer, PARTICLE_COUNT, 1);
        }

        // Draw ImGui
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *m_GraphicsCommandBuffers[m_FrameIndex]);
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

    void recordComputeCommandBuffer() const {
        auto & commandBuffer = m_ComputeCommandBuffers[m_FrameIndex];
        commandBuffer.reset();
        commandBuffer.begin({});

        m_PipelineManager->setActiveCommandBuffer(commandBuffer);
        m_PipelineManager->setAndBindActivePipeline(COMPUTE_PIPELINE_NAME);
        m_PipelineManager->prepareComputeConstants(m_ComputePushConstants.addressThisFrame, m_ComputePushConstants.addressLastFrame, m_DeltaTime);

        m_PipelineManager->bindComputePushConstants();

        if (m_ParticlesEnabled) {
            // Correlates to [numthreads(x, y, z)] inside the shader
            commandBuffer.dispatch(PARTICLE_COUNT / 256, 1, 1);
        }
        commandBuffer.end();
    }

    void createSyncObjects() {
        assert(m_InFlightFences.empty());

        vk::SemaphoreTypeCreateInfo semaphoreType = {
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0
        };
        m_Semaphore = m_Instance.getRaiiDevice().createSemaphore({ .pNext = &semaphoreType });

        for (size_t i = 0; i < m_SwapChainImages.size(); ++i) {
            m_InFlightFences.emplace_back(m_Instance.getRaiiDevice(), vk::FenceCreateInfo {});
        }
    }

    void cleanupSwapChain() {
        for (auto & imageView : m_SwapChainImageViews) {
            m_VulkanResourceService->freeResources(imageView);
        }
        m_SwapChainImageViews.clear();
        m_SwapChain = nullptr; // doing this automatically calls the destructor which in turn calls swapChain.clear()
    }

    void cleanupImageResources() {
        m_VulkanResourceService->freeResources(m_ColorImage, m_ColorImageMemory, m_ColorImageView);
        m_VulkanResourceService->freeResources(m_DepthImage, m_DepthImageMemory, m_DepthImageView);
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

        m_Instance.getDevice().waitIdle();

        m_DrawExecutor->destroy();
        m_PipelineManager->destroyAll();

        cleanupSwapChain();
        createSwapChain();

        m_DrawExecutor->initialize();
        m_PipelineManager->createComputePipeline();
        m_PipelineManager->createGraphicsPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);
        m_PipelineManager->createPointGraphicsPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);
        m_PipelineManager->createMinimapPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);

        cleanupImageResources();
        createImageResources();
    }

    void recompileShadersAndRecreatePipeline() {
        m_Instance.getDevice().waitIdle();

        m_PipelineManager->destroyAll();
        m_DrawExecutor->destroy();

        m_DrawExecutor->initialize();
        m_PipelineManager->createComputePipeline();
        m_PipelineManager->createGraphicsPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);
        m_PipelineManager->createPointGraphicsPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);
        m_PipelineManager->createMinimapPipeline(m_MsaaSamples, m_SwapChainSurfaceFormat);

        m_ShadersSetToReload = false;
    }

private:
    std::shared_ptr<VulkanResourceService> m_VulkanResourceService = nullptr;
    std::shared_ptr<DeviceMapper> m_DeviceMapper = nullptr;
    std::shared_ptr<DescriptorManager> m_DescriptorManager = nullptr;
    std::shared_ptr<DrawExecutor> m_DrawExecutor = nullptr;
    std::shared_ptr<PipelineManager> m_PipelineManager = nullptr;
    std::shared_ptr<ModelParser> m_ModelParser = nullptr;
    std::shared_ptr<TextureManager> m_TextureManager = nullptr;

    AssetManager m_AssetManager {};
    EntitySystem m_EntitySystem {};

    Camera m_Camera {};

    float m_DeltaTime = 0.0f;
    float m_FrameTimeMs = 0.0f;
    uint32_t m_Fps = 0;

    uint32_t m_MazeWidth = 0;
    uint32_t m_MazeHeight = 0;

    // Dynamic shader data
    LightMode m_LightMode = LightMode::Phong;
    bool m_ParticlesEnabled = false;

    bool m_IsWindowFocused = true;
    bool m_ShouldCursorBeVisible = false;
    bool m_CanUiFocusBeToggled = false;     // Seems janky, should probably move to a more robust input state handling method later
    bool m_DrawLightCubes = false;
    bool m_DrawMinimap = false;

    std::vector<const char *> m_LightNames {};
    bool m_LightNamesOutOfDate = true;

    std::vector<const char *> m_ObjectNames {};
    bool m_ObjectNamesOutOfDate = true;

    GLFWwindow * m_Window = nullptr;

    VulkanInstance m_Instance {};

    vk::raii::SwapchainKHR m_SwapChain = nullptr;
    std::vector<vk::Image> m_SwapChainImages;
    vk::SurfaceFormatKHR m_SwapChainSurfaceFormat;
    vk::Extent2D m_SwapChainExtent;
    std::vector<vk::ImageView> m_SwapChainImageViews;
    bool m_FramebufferResized = false;

    bool m_ShadersSetToReload = false;

    std::vector<vk::raii::CommandBuffer> m_GraphicsCommandBuffers {};
    std::vector<vk::raii::CommandBuffer> m_ComputeCommandBuffers {};

    vk::raii::Semaphore m_Semaphore = nullptr;
    uint64_t m_TimelineValue = 0;
    std::vector<vk::raii::Fence> m_InFlightFences {};
    uint32_t m_FrameIndex = 0;

    std::vector<VulkanComputeBufferData> m_ComputeDataBuffers {};
    ComputePushConstants m_ComputePushConstants {};

    uint32_t m_MinimapSize = 0;
    VulkanBufferData m_MinimapBuffer {};

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
