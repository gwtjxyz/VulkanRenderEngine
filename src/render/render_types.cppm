module;

#include <cstddef>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

export module render_types;

import std.compat;

import glm;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

export struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() {
        return {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))
        };
    }

    bool operator==(const Vertex & other) const {
        return pos == other.pos && normal == other.normal && texCoord == other.texCoord;
    }
};

// Hash function for Vertex struct
export template <>
struct std::hash<Vertex> {
    size_t operator()(Vertex const & vertex) const noexcept {
        return ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.normal) << 1)) << 1) ^
            (hash<glm::vec2>()(vertex.texCoord) << 1);
    }
};

// Replacement for UBOs that we address using "bindless" (buffer device address extension)
export struct ShaderData {
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 model;
    alignas(16) glm::vec4 lightPos { 0.0f, -10.0f, 10.0f, 0.0f };
    alignas(4) uint32_t selected { 1 };
    alignas(4) uint32_t lightingEnabled = { 1 };
};

export struct Plane {
    // Some point on the plane
    glm::vec3 point = glm::vec3(0.0f);
    // Plane's normal - unit vector
    glm::vec3 normal = { 0.0f, 1.0f, 0.0f };
};

// TODO
export struct BoundingBox {
    void transform(const glm::mat4 & transformationMatrix) {
        // TODO
    }
};
