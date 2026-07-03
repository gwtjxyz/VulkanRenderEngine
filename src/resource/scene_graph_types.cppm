module;

#include <cstdint>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

export module scene_graph_types;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import glm;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

import vulkan_resource_service;
import utils;

export struct Transform {
    glm::vec3 translation = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4x4 asMatrix() const {
        return glm::translate(
            glm::mat4_cast(rotation) * glm::scale(glm::mat4x4(1.0f), scale),
            translation
        );
    }

    // Assuming non-negative scaling
    static Transform fromMatrix(const glm::mat4x4 & mat) {
        Transform transform {};

        // TODO: check if I'm messing up column- vs row-major adjustments (GLM is column-major)
        // Translation is just the last column of the matrix
        transform.translation.x = mat[3][0];
        transform.translation.y = mat[3][1];
        transform.translation.z = mat[3][2];

        // Scale is the length of the first three column vectors
        transform.scale.x = glm::length(mat[0]);
        transform.scale.y = glm::length(mat[1]);
        transform.scale.z = glm::length(mat[2]);

        // To compute rotation, we first need to extract translation and scale from the input matrix
        // by dividing the first three column vectors by their scaling factors
        glm::mat3x3 pureRotation = glm::mat3(mat);
        pureRotation[0] /= transform.scale.x;
        pureRotation[1] /= transform.scale.y;
        pureRotation[2] /= transform.scale.z;

        // Convert rotation matrix to quaternion
        transform.rotation = glm::quat_cast(pureRotation);

        return transform;
    }
};

export enum class TransformUpdateFlags : uint32_t {
    Translation = 1 << 0,
    Rotation = 1 << 1,
    Scale = 1 << 2
};

template<>
struct FlagTraits<TransformUpdateFlags> {
    static constexpr bool isBitmask = true;
    static constexpr BitFlags<TransformUpdateFlags> allFlags =
        TransformUpdateFlags::Translation | TransformUpdateFlags::Rotation | TransformUpdateFlags::Scale;
};

// Forward declaration
export struct Scene;

// Temporary node implementation - subject to change/removal
// Should use flecs for this maybe?
export struct Node {
    Node * parent = nullptr;
    Scene * scene = nullptr;
    std::string name;
    std::vector<uint32_t> childIndices;

    std::optional<uint32_t> meshIndex;
    Transform localTransform;
};

export struct Material;

export struct Primitive {
    Scene * parent = nullptr;

    uint32_t vertexOffset;      // index, not bytes!
    uint32_t firstIndex;
    uint32_t indexCount;

    uint32_t materialIndex;

    Material * associatedMaterial() const;
};

export struct Mesh {
    Node * parent = nullptr;
    std::vector<Primitive> primitives;

    bool globalTransformDirty = true;
    glm::mat4x4 globalTransform;
};

export struct DrawCallData {
    uint32_t drawIndex;
    vk::Buffer vertexBuffer;
    uint32_t vertexOffset;
    vk::Buffer indexBuffer;
    uint32_t firstIndex;
    uint32_t indexCount;
};

// Keep it simple for now, expand later
struct Material {
    glm::vec4 color { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct Scene {
    VulkanBufferData vertexBuffer;
    VulkanBufferData indexBuffer;

    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Node> nodes;

    Transform sceneTransform;

    std::vector<uint32_t> rootIndices;

    Node * nodeAtIndex(const uint32_t index) {
        return &nodes.at(index);
    }
};

Material * Primitive::associatedMaterial() const {
    return &parent->materials.at(materialIndex);
}
