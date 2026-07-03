module;

#include <cstddef>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#include <cstdint> // for max value macros

export module render_types;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import glm;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

export using hlsl_bool = uint32_t;

export constexpr uint32_t INDEX_UNSET = UINT32_MAX;

export enum class LightMode : int32_t {
    Off = 0,
    Phong = 1,
    Gooch = 2
};

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
export struct MeshData {
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 model;
    alignas(16) uint32_t textureIndex = { 0 };
};

export struct LightData {
    alignas(16) glm::vec4 lightPos {};
};

// Same data layout as Vertex struct so we can use the same shader for both, even though most of this data isn't used
export struct Particle {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec2 velocity;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return {.binding = 0, .stride = sizeof(Particle), .inputRate = vk::VertexInputRate::eVertex};
    }

    // Velocity is only used inside compute shader = don't need to have an attribute description for it
    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
        return {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, position)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, color)),
        };
    }
};

// SSBO pointers for compute shaders are referenced from here
export struct ComputePushConstants {
    vk::DeviceAddress addressThisFrame;
    vk::DeviceAddress addressLastFrame;
    float deltaTime;
    uint32_t particlesEnabled;
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
