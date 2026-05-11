module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

export module rendergraph;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

import render_service_locator;
import vulkan_resource_service;

import std.compat;

// Rendergraph implementation for automated dependency management
export class Rendergraph {
private:
    // Resource description and management structure
    // Represents Image resource used during rendering (textures)
    struct Resource {
        std::string name;                           // Human-readable identifier for debugging and referencing
        vk::Format format;                          // Pixel format (RGBA8, Depth24Stencil8, etc)
        vk::Extent2D extent;                        // Dimensions in pixels for 2D resources
        vk::ImageUsageFlags usage;                  // How this resource will be used (color attachment, texture, etc.)
        vk::ImageLayout initialLayout;               // Expected layout when frame begins
        vk::ImageLayout finalLayout;                 // Required layout when frame ends

        // Actual GPU resources - populated during compilation
        vk::Image image = nullptr;                  // GPU image object
        vk::DeviceMemory memory = nullptr;          // Backing memory allocation
        vk::ImageView view = nullptr;               // Shader-accessible view of the image
        bool isLoaded = false;
    };

    // Render pass representation within the graph structure
    // Each pass represents a distinct rendering operation with defined inputs and outputs
    struct RenderPass {
        std::string name;                                       // Descriptive name for debugging and profiling
        std::vector<std::string> inputs;                        // Resources this pass reads from (dependencies)
        std::vector<std::string> outputs;                       // Resources this pass writes to (products)
        std::function<void(vk::CommandBuffer &)> executeFunc;   // The actual rendering code
    };

public:
    explicit Rendergraph(vk::Device & device) : m_Device(device) {
        m_VulkanResourceService = Locator::getVulkanResourceService();
    }

    // Resource registration interface for declaring all resources used during rendering
    // This method establishes resource metadata without creating actual GPU resources
    void addResource(
        const std::string & name, vk::Format format, vk::Extent2D extent,
        vk::ImageUsageFlags usage, vk::ImageLayout initialLayout, vk::ImageLayout finalLayout
    ) {
        Resource resource = {
            .name = name,
            .format = format,
            .extent = extent,
            .usage = usage,
            .initialLayout = initialLayout,
            .finalLayout = finalLayout
        };

        m_Resources[name] = std::move(resource);
    }

    void removeResource(const std::string & name) {
        auto resourceToRemove = m_Resources.find(name);
        if (resourceToRemove == m_Resources.end()) {
            return;
        }

        if (resourceToRemove->second.isLoaded) {
            // TODO maybe don't force a recompilation of the entire pass every time we remove a resource?
            decompile();
        }

        m_Resources.erase(resourceToRemove);
    }

    // Render pass registration interface for defining rendering operations and their dependencies
    // This method establishes the logical structure of rendering without immediate execution
    void addRenderPass(
        const std::string & name,
        const std::vector<std::string> & inputs,
        const std::vector<std::string> & outputs,
        const std::function<void(vk::CommandBuffer &)> & executeFunc
    ) {
        if (m_IsCompiled) {
            decompile();
        }

        // Search for an already existing pass with that name
        const auto & it = std::ranges::find_if(
            m_RenderPasses, [&name](const RenderPass & pass) {
                return pass.name == name;
            }
        );
        if (it != m_RenderPasses.end()) {
            // Probably we want to replace the render pass with that name
            // TODO log this action?
            it->inputs = inputs;
            it->outputs = outputs;
            it->executeFunc = executeFunc;

            return;
        }

        RenderPass renderPass = {
            .name = name,
            .inputs = inputs,
            .outputs = outputs,
            .executeFunc = executeFunc
        };

        m_RenderPasses.push_back(renderPass);
    }

    void removeRenderPass(const std::string & name) {
        for (auto it = m_RenderPasses.begin(); it != m_RenderPasses.end(); ++it) {
            if (it->name != name) continue;

            if (m_IsCompiled) {
                decompile();
            }

            m_RenderPasses.erase(it);
            break;
        }
    }

    // Rendergraph compilation - transforms declarative descriptions into executable pipeline
    // This method performs dependency analysis, resource allocation, and execution planning
    void compile() {
        if (!m_IsCompiled) {
            decompile();    // Make sure we don't leak memory
        }
        // Dependency Graph Construction
        // Build bidirectional dependency relationships between passes
        std::vector<std::vector<size_t>> dependencies(m_RenderPasses.size());   // What each pass depends on
        std::vector<std::vector<size_t>> dependents(m_RenderPasses.size());     // What depends on each pass

        // Track which pass produces each resource (write-after-write dependencies)
        std::unordered_map<std::string, size_t> resourceWriters;

        // Dependency Discovery Through Resource Usage Analysis
        // Analyze each pass to determine data flow relationships
        for (size_t i = 0; i < m_RenderPasses.size(); ++i) {
            const auto & renderPass = m_RenderPasses[i];

            // Process input dependencies - this pass must wait for producers
            for (const auto & input : renderPass.inputs) {
                auto it = resourceWriters.find(input);
                if (it != resourceWriters.end()) {
                    // Found the pass that produces this input - create dependency link
                    dependencies[i].push_back(it->second);
                    dependents[it->second].push_back(i);
                }
            }

            // Register output production - subsequent passes may depend on these
            for (const auto & output : renderPass.outputs) {
                // Record this pass as producer
                resourceWriters[output] = i;
            }
        }

        // Topological sort for optimal execution order
        // Use DFS to compute valid execution sequence while detecting cycles
        std::vector<bool> visited(m_RenderPasses.size(), false); // Track completed nodes
        std::vector<bool> inStack(m_RenderPasses.size(), false); // Track current recursion path

        std::function<void(size_t)> visit = [&](size_t node) {
            if (inStack[node]) {
                // Cycle detection - circular dependency found
                throw std::runtime_error("Cycle detected in rendergraph");
            }

            if (visited[node]) {
                // Already processed this node and its dependencies
                return;
            }

            inStack[node] = true; // Mark as currently being processed

            // Recursively process all dependent passes first (post-order traversal)
            for (auto dependent : dependents[node]) {
                visit(dependent);
            }

            inStack[node] = false; // Remove from current path
            visited[node] = true; // Mark as completely processed
            m_ExecutionOrder.push_back(node); // Add to execution sequence
        };

        // Process all unvisited nodes to handle disconnected graph components
        for (size_t i = 0; i < m_RenderPasses.size(); ++i) {
            if (!visited[i]) {
                visit(i);
            }
        }

        // Automatic Synchronization object creation
        // Generate semaphores for all dependencies identified during analysis
        for (size_t i = 0; i < m_RenderPasses.size(); ++i) {
            for (auto dep : dependencies[i]) {
                // Create a GPU semaphore for this dependency relationship
                // The dependent pass will wait on this semaphore before executing
                m_Semaphores.emplace_back(m_Device.createSemaphore({}));
                m_SemaphoreSignalWaitPairs.emplace_back(dep, i); // (producer, consumer) pair
            }
        }

        // Physical resource allocation and creation
        // Transform resource descriptions into acutla GPU objects
        for (auto & [name, resource] : m_Resources) {
            // Configure image creation parameters based on resource description
            auto resourceImageData = m_VulkanResourceService->createGenericResources(
                resource.format,
                resource.extent,
                vk::SampleCountFlagBits::e1,                                // Disable AA for simplicity
                resource.usage,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                1,                                                          // Single mip level for simplicity
                vk::ImageAspectFlagBits::eColor
            );
            resource.image = resourceImageData.image;
            resource.memory = resourceImageData.imageMemory;
            resource.view = resourceImageData.imageView;
            resource.isLoaded = true;
        }

        m_IsCompiled = true;
    }

    // Cleanup method to free allocated resource memory and reset render graph state
    void decompile() {
        if (!m_IsCompiled) return;

        // Wait for current execution to finish first
        m_Device.waitIdle();

        for (const auto & semaphore : m_Semaphores) {
            m_Device.destroySemaphore(semaphore);
        }
        m_ExecutionOrder.clear();
        m_SemaphoreSignalWaitPairs.clear();

        for (auto & [name, resource] : m_Resources) {
            if (!resource.isLoaded) continue;

            m_Device.destroyImageView(resource.view);
            m_Device.destroyImage(resource.image);
            m_Device.freeMemory(resource.memory);
            resource.isLoaded = false;
        }

        m_IsCompiled = false;
    }

    // Resource access interface for retrieving compiled resources
    Resource * getResource(const std::string & name) {
        auto it = m_Resources.find(name);
        return it != m_Resources.end() ? &it->second : nullptr;
    }

    // Rendergraph execution engine - coordinates pass execution with automatic synchronization
    // This method transforms the compiled rendergraph into actual GPU work
    void execute(vk::CommandBuffer & commandBuffer, vk::Queue & queue) {
        if (!m_IsCompiled) {
            throw std::runtime_error("Cannot execute render graph before compiling it!");
        }
        // Execution state management for dynamic synchronization
        std::vector<vk::CommandBuffer> cmdBuffers;              // Command buffer storage
        std::vector<vk::Semaphore> waitSemaphores;              // Synchronization dependencies for current pass
        std::vector<vk::PipelineStageFlags> waitStages;         // Pipeline stages to wait on
        std::vector<vk::Semaphore> signalSemaphores;            // Semaphores to signal after current pass

        // Ordered pass execution with automatic dependency management
        // Execute each pass in the computed dependency-safe order
        for (auto passIdx : m_ExecutionOrder) {
            const auto & pass = m_RenderPasses[passIdx];

            // Synchronization setup - collect dependencies for current pass
            // Determine what this pass must wait for before executing
            waitSemaphores.clear();
            waitStages.clear();

            for (size_t i = 0; i < m_SemaphoreSignalWaitPairs.size(); ++i) {
                if (m_SemaphoreSignalWaitPairs[i].second == passIdx) {
                    // This pass depends on the completion of another pass
                    waitSemaphores.push_back(m_Semaphores[i]); // Wait for dependency completion
                    waitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput); // Wait at output stage
                }
            }

            // Collect semaphores that this pass will signal for dependent passes
            signalSemaphores.clear();
            for (size_t i = 0; i < m_SemaphoreSignalWaitPairs.size(); ++i) {
                if (m_SemaphoreSignalWaitPairs[i].first == passIdx) {
                    // Other passes depend on this pass's completion
                    signalSemaphores.push_back(m_Semaphores[i]);
                }
            }

            // Command buffer preparation and resource layout transitions
            // Set up command recording and transition resources to appropriate layouts
            vk::Result res = commandBuffer.begin({}); // Begin command recording, TODO handle errors

            // Transition input resources to shader-readable layouts
            for (const auto & input : pass.inputs) {
                auto & resource = m_Resources[input];

                transitionImageLayout(
                    resource.initialLayout,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    resource.image,
                    vk::ImageAspectFlagBits::eColor,
                    vk::AccessFlagBits2::eMemoryWrite,
                    vk::AccessFlagBits2::eShaderRead,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::PipelineStageFlagBits2::eFragmentShader,
                    commandBuffer
                );

                // transitionImageLayout(
                //     commandBuffer,
                //     resource.image,
                //     resource.initialLayout,
                //     vk::ImageLayout::eShaderReadOnlyOptimal
                // );
            }

            // Transition output resources to render target layouts
            for (const auto & output : pass.outputs) {
                auto & resource = m_Resources[output];

                transitionImageLayout(
                    resource.initialLayout,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    resource.image,
                    vk::ImageAspectFlagBits::eColor,
                    vk::AccessFlagBits2::eMemoryRead,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    commandBuffer
                );
            }

            // Pass execution - execute the actual rendering logic
            // call the user-provided rendering function with prepared command buffer
            pass.executeFunc(commandBuffer);

            // Final layout transitions - prepare resources for subsequent use
            // Transition output resources to their final required layouts
            for (const auto & output : pass.outputs) {
                auto & resource = m_Resources[output];

                transitionImageLayout(
                    vk::ImageLayout::eColorAttachmentOptimal,
                    resource.finalLayout,
                    resource.image,
                    vk::ImageAspectFlagBits::eColor,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::AccessFlagBits2::eMemoryRead,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    commandBuffer
                );
            }

            // Command submission with synchronization
            // Submit command buffer with appropriate dependency and signaling semaphores
            commandBuffer.end();

            vk::SubmitInfo submitInfo;
            submitInfo.setWaitSemaphoreCount(static_cast<uint32_t>(waitSemaphores.size()))
                .setPWaitSemaphores(waitSemaphores.data())
                .setPWaitDstStageMask(waitStages.data())
                .setCommandBufferCount(1)
                .setPCommandBuffers(&commandBuffer)
                .setSignalSemaphoreCount(static_cast<uint32_t>(signalSemaphores.size()))
                .setPSignalSemaphores(signalSemaphores.data());

            res = queue.submit(1, &submitInfo, nullptr);
        }
    }

private:
#if 1
    void transitionImageLayout(
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::Image image,
        vk::ImageAspectFlags imageAspectMask,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask,
        vk::CommandBuffer & commandBuffer
    ) {
        vk::ImageMemoryBarrier2 barrier;
        barrier.setOldLayout(oldLayout)
            .setNewLayout(newLayout)
            .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
            .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
            .setImage(image)
            .setSubresourceRange({ imageAspectMask, 0, 1, 0, 1 })
            .setSrcAccessMask(srcAccessMask)
            .setDstAccessMask(dstAccessMask)
            .setSrcStageMask(srcStageMask)
            .setDstStageMask(dstStageMask);

        vk::DependencyInfo dependencyInfo;
        dependencyInfo.setDependencyFlags(vk::DependencyFlagBits::eByRegion)
            .setImageMemoryBarrierCount(1)
            .setImageMemoryBarriers(barrier);

        commandBuffer.pipelineBarrier2(dependencyInfo);
    }
#else
    // TODO make this usable
    void transitionImageLayout(
        vk::CommandBuffer & commandBuffer,
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout
    ) {
        vk::ImageMemoryBarrier2 barrier;
        barrier.setOldLayout(oldLayout)
            .setNewLayout(newLayout)
            .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
            .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
            .setImage(image);

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            // Common for preparing images for data uploads
            barrier.setSrcAccessMask(vk::AccessFlagBits2::eNone)                // No previous access to synchronize
                .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)          // Enable transfer write ops
                .setSrcStageMask(vk::PipelineStageFlagBits2::eTopOfPipe)        // No previous work to wait for
                .setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)         // Transfer operations can proceed
                .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
        } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            // Transfer-to-shader layout transitions
            barrier.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)       // Previous transfer writes must complete
                .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)             // Enable shader read access
                .setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)         // Transfer ops must complete
                .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)   // Fragment shaders can access
                .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
        } else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthAttachmentOptimal) {
            // Depth image transition
            barrier.setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)     // TODO shouldn't this be "read" instead?
                .setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
                .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
                .setSubresourceRange({ vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });
        } else {
            // Handle unsupported transition combinations
            // Could include additional common transition patterns as well
            throw std::invalid_argument("Unsupported layout transition!");
        }

        vk::DependencyInfo dependencyInfo;
        dependencyInfo.setDependencyFlags(vk::DependencyFlagBits::eByRegion)
            .setImageMemoryBarrierCount(1)
            .setImageMemoryBarriers(barrier);
        commandBuffer.pipelineBarrier2(dependencyInfo);
    }
#endif
    // Core data storage for the rendergraph system
    std::unordered_map<std::string, Resource> m_Resources;              // All resources referenced in the graph
    std::vector<RenderPass> m_RenderPasses;                             // All rendering passes in definition order
    std::vector<size_t> m_ExecutionOrder;                               // Computed optimal execution sequence

    // Automatic synchronization management
    // These objects ensure correct GPU execution order without manual barriers
    std::vector<vk::Semaphore> m_Semaphores;                            // GPU sync primitives
    std::vector<std::pair<size_t, size_t>> m_SemaphoreSignalWaitPairs;  // (signaling pass, waiting pass)

    vk::Device & m_Device;
    VulkanResourceService * m_VulkanResourceService = nullptr;

    bool m_IsCompiled = false;
};
