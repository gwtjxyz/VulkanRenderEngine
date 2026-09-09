module;

#ifdef DISABLE_IMPORT_STD
#include <filesystem>
#include <iostream>
#include <ranges>
#include <unordered_map>
#include <variant>
#endif

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

import bit_flags;
import device_mapper;
import render_types;
import vulkan_resource_service;

namespace SceneGraphTypes {
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
}

export class SceneGraphAsset {
private:
    using graph_index = uint32_t;
    using device_index = uint32_t;

    struct MeshPrimitiveIndex {
        graph_index meshIndex;
        graph_index primitiveIndex;

        bool operator==(const MeshPrimitiveIndex & other) const {
            return meshIndex == other.meshIndex && primitiveIndex == other.primitiveIndex;
        }
    };

    struct MPIndexHash {
        size_t operator()(const MeshPrimitiveIndex & mpIndex) const {
            return std::hash<uint32_t>()(mpIndex.meshIndex) ^
                (std::hash<uint32_t>()(mpIndex.primitiveIndex) << 1);
        }
    };

public:
    explicit SceneGraphAsset(SceneGraphTypes::Scene scene) : m_Scene(std::move(scene)), m_Loaded(false), m_DeviceMapper(DeviceMapperLocator::locate()) {}

    [[nodiscard]] bool isLoaded() const {
        return m_Loaded;
    }

    // Sets up indices inside DeviceMapper to facilitate uploading data to the GPU
    void load() {
        if (m_Loaded) return;

        // Not sure if this is needed but not calling clear was causing issues before so I'll do it just to be safe
        m_DrawMap.clear();
        m_TransformMap.clear();
        m_MaterialMap.clear();

        uint32_t materialGraphIndex = 0;
        for (auto & mat : m_Scene.materials) {
            // TODO update once we support textures in here
            device_index materialDeviceIndex = m_DeviceMapper->addMaterial(mat.color, INDEX_UNSET);
            m_MaterialMap.insert_or_assign(materialGraphIndex, materialDeviceIndex);
            m_DeviceMapper->getMaterial(materialDeviceIndex)->layout.materialTint = mat.color;

            materialGraphIndex++;
        }

        graph_index meshIndex = 0;
        for (auto & m : m_Scene.meshes) {
            // A mesh can have multiple primitives, but only one transform

            device_index transformDeviceIndex = m_DeviceMapper->addTransform();
            m_TransformMap.insert_or_assign(meshIndex, transformDeviceIndex);
            if (m.globalTransformDirty) {
                computeAndLoadGlobalMeshTransform(meshIndex, m);
                m.globalTransformDirty = false;
            }

            graph_index primitiveIndex = 0;
            for (auto & p : m.primitives) {
                MeshPrimitiveIndex mpIndex { meshIndex, primitiveIndex };
                // multiple primitives = need a separate draw for each of them
                device_index drawDeviceIndex = m_DeviceMapper->addDraw();
                m_DrawMap.insert_or_assign(mpIndex, drawDeviceIndex);
                m_DeviceMapper->getDraw(drawDeviceIndex)->layout = {
                    transformDeviceIndex, m_MaterialMap.at(p.materialIndex), true
                };

                primitiveIndex++;
            }

            meshIndex++;
        }
    }

    // Unloads data from the device mapper
    void unload() {
        if (!m_Loaded) return;

        m_Loaded = false;

        for (const auto & drawEntry : m_DrawMap | std::views::values) {
            m_DeviceMapper->removeDraw(drawEntry);
        }
        m_DrawMap.clear();
        for (const auto & materialEntry : m_MaterialMap | std::views::values) {
            m_DeviceMapper->removeMaterial(materialEntry);
        }
        m_MaterialMap.clear();
        for (const auto & transformEntry : m_TransformMap | std::views::values) {
            m_DeviceMapper->removeTransform(transformEntry);
        }
        m_TransformMap.clear();

        for (auto & m : m_Scene.meshes) {
            m.globalTransformDirty = true;
        }
    }

    // Frees up Vulkan resources associated with the object
    // Note: this is irreversible, to load data back in you'd have to re-import the asset entirely
    void destroy() {
        if (m_Loaded) {
            unload();
        }

        const auto * vulkanResourceService = VulkanResourceServiceLocator::locate();

        vulkanResourceService->freeResources(m_Scene.vertexBuffer);
        vulkanResourceService->freeResources(m_Scene.indexBuffer);
    }

    std::vector<SceneGraphTypes::DrawCallData> prepareDrawData() {
        std::vector<SceneGraphTypes::DrawCallData> drawData {};
        if (!m_Loaded) return drawData;

        /**
         * To draw each primitive, we need to do the following:
         * 1) set up push constants to index into appropriate GPU buffers
         * 2) bind vertices and indices as needed
         * 3) issue a draw command
         *
         * To set up push constants, we need the data inside the mesh struct
         * To bind the indices, we need the data inside the primitive struct
         *
         * That data should already be present if the asset is loaded, so
         * all we need to do is supply the constants using device mappings
         * and possibly update transforms if they are dirty
         *
         * for now we assume materials don't change on the fly and we can't disable
         * drawing individual mesh parts, only the whole asset
         */

        graph_index meshIndex = 0;
        for (auto & m : m_Scene.meshes) {
            if (!m.parent) continue;

            // Compute and update transform if needed
            if (m.globalTransformDirty) {
                computeAndLoadGlobalMeshTransform(meshIndex, m);
                m.globalTransformDirty = false;
            }

            graph_index meshPrimitiveIndex = 0;
            for (const auto & mp : m.primitives) {
                SceneGraphTypes::DrawCallData data {};
                data.drawIndex = m_DrawMap.at({ meshIndex, meshPrimitiveIndex });
                data.vertexBuffer = m_Scene.vertexBuffer.buffer;
                data.vertexOffset = mp.vertexOffset;
                data.indexBuffer = m_Scene.indexBuffer.buffer;
                data.firstIndex = mp.firstIndex;
                data.indexCount = mp.indexCount;
                drawData.emplace_back(data);

                meshPrimitiveIndex++;
            }

            meshIndex++;
        }

        return drawData;
    }

    void updateSceneTransform(const SceneGraphTypes::Transform & transform, BitFlags<TransformUpdateFlags> flags, bool reset = false) {
        if (flags & TransformUpdateFlags::Translation) {
            if (reset) {
                m_Scene.sceneTransform.translation = transform.translation;
            } else {
                m_Scene.sceneTransform.translation += transform.translation;
            }
        }

        if (flags & TransformUpdateFlags::Rotation) {
            if (reset) {
                m_Scene.sceneTransform.rotation = transform.rotation;
            } else {
                m_Scene.sceneTransform.rotation += transform.rotation;
            }
        }

        if (flags & TransformUpdateFlags::Scale) {
            if (reset) {
                m_Scene.sceneTransform.scale = transform.scale;
            } else {
                m_Scene.sceneTransform.scale += transform.scale;
            }
        }

        for (auto & m : m_Scene.meshes) {
            m.globalTransformDirty = true;
        }
    }

private:
    // Need to apply to bottom-most nodes
    static glm::mat4x4 computeGlobalTransform(const SceneGraphTypes::Node * node) {
        assert(node);

        const glm::mat4x4 localModelMatrix = node->localTransform.asMatrix();

        if (!node->parent) {
            return node->scene->sceneTransform.asMatrix() * localModelMatrix;
        }

        return computeGlobalTransform(node->parent) * localModelMatrix;
    }

    void computeAndLoadGlobalMeshTransform(graph_index meshIndex, SceneGraphTypes::Mesh & mesh) const {
        glm::mat4x4 globalTransformMatrix = computeGlobalTransform(mesh.parent);
        mesh.globalTransform = globalTransformMatrix;

        auto [translation, rotation, scale] = SceneGraphTypes::Transform::fromMatrix(mesh.globalTransform);
        auto * deviceTransform = m_DeviceMapper->getTransform(m_TransformMap.at(meshIndex));

        deviceTransform->layout.position.x = translation.x;
        deviceTransform->layout.position.y = translation.y;
        deviceTransform->layout.position.z = translation.z;

        deviceTransform->layout.rotation.x = rotation.x;
        deviceTransform->layout.rotation.y = rotation.y;
        deviceTransform->layout.rotation.z = rotation.z;
        deviceTransform->layout.rotation.w = rotation.w;

        deviceTransform->layout.scale.x = scale.x;
        deviceTransform->layout.scale.y = scale.y;
        deviceTransform->layout.scale.z = scale.z;
    }

private:
    SceneGraphTypes::Scene m_Scene {};
    bool m_Loaded;
    DeviceMapper * m_DeviceMapper;

    // different for each mesh+primitive combo
    std::unordered_map<MeshPrimitiveIndex, device_index, MPIndexHash> m_DrawMap {};
    // same for all primitives on a mesh, unique for each mesh
    std::unordered_map<graph_index, device_index> m_TransformMap {};
    // unique for each material
    std::unordered_map<graph_index, device_index> m_MaterialMap {};
};
