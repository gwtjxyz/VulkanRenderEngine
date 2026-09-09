module;

#include <cassert>
#include <cstdint>

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

#include "core/util_macros.h"

export module model_parser;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import glm;
import fastgltf;
import tinyobjloader;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

import device_mapper;
import render_types;
export import scene_graph_types;
import bit_flags;
import vulkan_resource_service;

// Constant values for processing sampler info
namespace SamplerProperties {
constexpr int32_t magFilterNearest = 9728;
constexpr int32_t magFilterLinear = 9829;

constexpr int32_t minFilterNearest = 9728;
constexpr int32_t minFilterLinear = 9729;
constexpr int32_t minFilterNearestMipmapNearest = 9984;
constexpr int32_t minFilterLinearMipmapNearest = 9985;
constexpr int32_t minFilterNearestMipmapLinear = 9986;
constexpr int32_t minFilterLinearMipmapLinear = 9987;

constexpr int32_t wrapSClampToEdge = 33071;
constexpr int32_t wrapSMirroredRepeat = 33648;
constexpr int32_t wrapSRepeat = 10497;
constexpr int32_t wrapSDefault = wrapSRepeat;

constexpr int32_t wrapTClampToEdge = 33071;
constexpr int32_t wrapTMirroredRepeat = 33648;
constexpr int32_t wrapTRepeat = 10497;
constexpr int32_t wrapTDefault = wrapTRepeat;
}

// Minimal number of extensions supported at first
auto supportedExtensions = fastgltf::Extensions::None;

// Simple, minimal data format for now - TODO expand
struct Attribute {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord0;
};

// Assume decomposed transforms because we use the parsing option to guarantee it
SceneGraphTypes::Transform extractLocalTransform(const fastgltf::Node & node) {
    SceneGraphTypes::Transform transform {};
    // If transforms are null, assume default transforms
    if (const auto * trs = std::get_if<fastgltf::TRS>(&node.transform); trs != nullptr) {
        transform.translation.x = trs->translation.x();
        transform.translation.y = trs->translation.y();
        transform.translation.z = trs->translation.z();

        transform.rotation.x = trs->rotation.x();
        transform.rotation.y = trs->rotation.y();
        transform.rotation.z = trs->rotation.z();
        transform.rotation.w = trs->rotation.w();

        transform.scale.x = trs->scale.x();
        transform.scale.y = trs->scale.y();
        transform.scale.z = trs->scale.z();
    }
    return transform;
}

export class ModelParser;

GENERATE_LOCATOR(ModelParser)

class ModelParser {
public:
    ModelParser() : m_Parser(supportedExtensions) {}

    std::optional<SceneGraphAsset> load(const std::filesystem::path & path) {
        auto extension = path.extension();
        if (extension == ".gltf") {
            return loadGltf(path);
        }
        if (extension == ".obj") {
            return loadObj(path);
        }

        std::cout << "Unsupported extension " << extension.string() << " for parsing 3D assets" << std::endl;
        return {};
    }

    std::optional<SceneGraphAsset> loadGltf(const std::filesystem::path & path) {
        auto startTime = std::chrono::steady_clock::now();

        auto data = fastgltf::GltfDataBuffer::FromPath(path);
        if (data.error() != fastgltf::Error::None) {
            // The file couldn't be loaded, or the buffer couldn't be allocated
            return {};
        }

        constexpr auto parsingOptions = fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::LoadExternalBuffers;

        auto asset = m_Parser.loadGltf(data.get(), path.parent_path(), parsingOptions);
        if (auto error = asset.error(); error != fastgltf::Error::None) {
            // Some error occurred while reading the buffer, parsing the JSON, or validating the data
            return {};
        }

        // Assume one scene for now
        SceneGraphTypes::Scene scene {};

        // Load materials
        // TODO expand - this is very basic for now
        for (auto & mat : asset->materials) {
            auto & color = mat.pbrData.baseColorFactor;
            scene.materials.emplace_back(glm::vec4(color.x(), color.y(), color.z(), color.w()));
        }
        // Add dummy material in case there are none in the model
        if (scene.materials.empty()) {
            scene.materials.emplace_back(glm::vec4(1.0f, 0.25f, 1.0f, 1.0f));
        }

        std::vector<Attribute> attributesToStore; // we will turn this into a Vulkan buffer later
        std::vector<uint32_t> indicesToStore;
        attributesToStore.resize(0);
        indicesToStore.resize(0);

        // Load vertex data
        for (auto & m : asset->meshes) {
            SceneGraphTypes::Mesh mesh {};

            for (auto & mp : m.primitives) {
                SceneGraphTypes::Primitive primitive {};
                // vkCmdDrawIndexed requires indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
                // indexCount and firstIndex we can get from indices accessor
                // vertexOffset we can get from attributesToStore
                // instanceCount and firstInstance we will provide from within the engine - don't need to worry about it here

                size_t oldAttributesSize = attributesToStore.size();
                primitive.vertexOffset = oldAttributesSize;

                if (mp.type != fastgltf::PrimitiveType::Triangles) {
                    throw std::runtime_error("Unsupported glTF: only supporting triangle primitives for now.");
                }

                // Required attributes: position and normal
                auto * positionAttrib = mp.findAttribute("POSITION");
                auto * normalAttrib = mp.findAttribute("NORMAL");

                if (positionAttrib == mp.attributes.end()) {
                    throw std::runtime_error("Unsupported glTF: POSITION attribute not defined.");
                }
                if (normalAttrib == mp.attributes.end()) {
                    throw std::runtime_error("Unsupported glTF: NORMAL attribute not defined.");
                }

                auto & posAccessor = asset->accessors[positionAttrib->accessorIndex];
                auto & normAccessor = asset->accessors[normalAttrib->accessorIndex];

                // Assume there's the same amount of POSITION/NORMAL/TEXCOORD_0 values
                attributesToStore.resize(oldAttributesSize + posAccessor.count);

                // TODO use fastgltf's glm stuff?

                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                    asset.get(), posAccessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
                        attributesToStore[oldAttributesSize + idx].position.x = pos.x();
                        attributesToStore[oldAttributesSize + idx].position.y = pos.y();
                        attributesToStore[oldAttributesSize + idx].position.z = pos.z();
                    }
                );

                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                    asset.get(), normAccessor, [&](fastgltf::math::fvec3 norm, std::size_t idx) {
                        attributesToStore[oldAttributesSize + idx].normal.x = norm.x();
                        attributesToStore[oldAttributesSize + idx].normal.y = norm.y();
                        attributesToStore[oldAttributesSize + idx].normal.z = norm.z();
                    }
                );

                // If texcoords exist, use them, otherwise fill them up with zeros
                auto * texcoordAttrib = mp.findAttribute("TEXCOORD_0");
                if (texcoordAttrib != mp.attributes.end()) {
                    auto & texAccessor = asset->accessors[texcoordAttrib->accessorIndex];

                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                        asset.get(), texAccessor, [&](fastgltf::math::fvec2 texcoord, std::size_t idx) {
                            attributesToStore[oldAttributesSize + idx].texcoord0.x = texcoord.x();
                            attributesToStore[oldAttributesSize + idx].texcoord0.y = texcoord.y();
                        }
                    );
                } else {
                    for (size_t idx = 0; idx < posAccessor.count; ++idx) {
                        attributesToStore[oldAttributesSize + idx].texcoord0.x = 0.0f;
                        attributesToStore[oldAttributesSize + idx].texcoord0.y = 0.0f;
                    }
                }

                // indices
                size_t oldIndicesSize = indicesToStore.size();
                // This will throw if there is no index buffer, but that's okay (at least for now)
                auto & indicesAccessor = asset->accessors[mp.indicesAccessor.value()];
                indicesToStore.resize(oldIndicesSize + indicesAccessor.count);
                // assume indices are uint32 (not sure if correct)
                fastgltf::iterateAccessorWithIndex<uint32_t>(
                    asset.get(), indicesAccessor, [&](uint32_t index, std::size_t idx) {
                        indicesToStore[oldIndicesSize + idx] = index;
                    }
                );

                primitive.firstIndex = oldIndicesSize;
                primitive.indexCount = indicesAccessor.count;

                primitive.materialIndex = mp.materialIndex.value_or(0);

                mesh.primitives.emplace_back(primitive);
            }

            scene.meshes.emplace_back(mesh);
        }

        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();
        scene.vertexBuffer = vulkanResourceService->createVulkanBuffer(
            attributesToStore.size() * sizeof(Attribute),
            vk::BufferUsageFlagBits::eVertexBuffer,
            attributesToStore.data()
        );
        scene.indexBuffer = vulkanResourceService->createVulkanBuffer(
            indicesToStore.size() * sizeof(uint32_t),
            vk::BufferUsageFlagBits::eVertexBuffer,
            attributesToStore.data()
        );

        // Traverse and load node tree
        // first, put each node into scene struct
        for (auto & n : asset->nodes) {
            SceneGraphTypes::Node node {};
            node.scene = &scene;
            node.name = n.name;
            node.localTransform = extractLocalTransform(n);
            if (n.meshIndex.has_value()) {
                node.meshIndex = n.meshIndex.value();
            } else {
                node.meshIndex = std::nullopt;
            }

            scene.nodes.emplace_back(node);
        }

        // Then, set up root nodes
        fastgltf::Scene gltfScene = asset->scenes.at(0);
        for (auto rootIndices : gltfScene.nodeIndices) {
            scene.rootIndices.emplace_back(rootIndices);
        }

        // Then, populate child/parent relations
        size_t nodeIndex = 0;
        for (auto & n : asset->nodes) {
            SceneGraphTypes::Node * node = scene.nodeAtIndex(nodeIndex);
            for (auto childIdx : n.children) {
                node->childIndices.emplace_back(childIdx);
                scene.nodeAtIndex(childIdx)->parent = node;
            }
            if (node->meshIndex.has_value()) {
                scene.meshes.at(node->meshIndex.value()).parent = node;
            }

            nodeIndex++;
        }

        std::chrono::duration<double, std::milli> msSpentParsing = std::chrono::steady_clock::now() - startTime;
        std::cout << "[INFO] Parsing glTF model at path " << path << " took " << msSpentParsing.count() << "ms" << std::endl;

        SceneGraphAsset sceneGraph { scene };

        return sceneGraph;
    }

    std::optional<SceneGraphAsset> loadObj(const std::filesystem::path & path) {
        auto startTime = std::chrono::steady_clock::now();

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        // TODO support loading materials
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;
        std::unordered_map<Vertex, uint32_t> uniqueVertices {};

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // TODO wchar support?
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str())) {
            std::println("Error loading OBJ model from path {}: {} {}", path.string(), warn, err);
            return {};
        }

        for (const auto & shape : shapes) {
            for (const auto & index : shape.mesh.indices) {
                Vertex vertex {};
                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                // OBJ assumes 0 = bottom of the image, but Vulkan works with 0 = top of the image, so we flip y coord
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
                vertex.normal = {
                    attrib.normals[3 * index.normal_index],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };

                if (!uniqueVertices.contains(vertex)) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
        }

        // For OBJ files, we create a simple scene graph structure with a single node/mesh/material/attribute

        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();
        SceneGraphTypes::Scene scene {};
        scene.vertexBuffer = vulkanResourceService->createVulkanBuffer(
            sizeof(vertices[0]) * vertices.size(),
            vk::BufferUsageFlagBits::eVertexBuffer,
            vertices.data()
        );
        scene.indexBuffer = vulkanResourceService->createVulkanBuffer(
            sizeof(indices[0]) * indices.size(),
            vk::BufferUsageFlagBits::eIndexBuffer,
            indices.data()
        );

        // Add one root node
        SceneGraphTypes::Node rootNode {};
        rootNode.scene = &scene;
        rootNode.name = "root";
        rootNode.meshIndex = 0;
        scene.nodes.emplace_back(rootNode);
        scene.rootIndices.emplace_back(0);

        // Add one material
        SceneGraphTypes::Material material {};
        scene.materials.emplace_back(material);

        // Add one primitive
        SceneGraphTypes::Primitive primitive {};
        primitive.parent = &scene;
        primitive.materialIndex = 0;

        // Add one mesh
        SceneGraphTypes::Mesh mesh {};
        mesh.parent = scene.nodeAtIndex(0);
        mesh.primitives.emplace_back(primitive);
        scene.meshes.emplace_back(mesh);

        std::chrono::duration<double, std::milli> msSpentParsing = std::chrono::steady_clock::now() - startTime;
        std::cout << "[INFO] Parsing OBJ model at path " << path << " took " << msSpentParsing.count() << "ms" << std::endl;

        SceneGraphAsset sceneGraph { scene };

        return sceneGraph;
    }

private:
    fastgltf::Parser m_Parser;
};
