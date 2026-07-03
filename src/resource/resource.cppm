module;

#include <cassert>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#ifdef DISABLE_IMPORT_STD
#include <filesystem>
#include <memory>
#include <print>
#include <typeindex>
#include <unordered_map>
#endif

#include <cstdint>

export module resource;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import device_mapper;
import model_parser;
import platform;
import pipeline_manager;
import render_types;
import vulkan_resource_service;

import glm;
import tinyobjloader;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

// Resource base class
export class Resource {
public:
    explicit Resource() {}
    virtual ~Resource() = default;
    Resource(const Resource &) = delete;
    Resource(const Resource &&) = delete;
    Resource & operator=(const Resource &) = delete;
    Resource & operator=(const Resource &&) = delete;

    // Core resource identity and state access methods
    [[nodiscard]] const std::string & getId() const {
        return m_ResourceId;
    }

    [[nodiscard]] bool isLoaded() const {
        return m_Loaded;
    }

    // Virtual interface for resource-specific loading and unloading behaviour
    bool load() {
        m_Loaded = doLoad();
        return m_Loaded;
    }

    void unload() {
        doUnload();
        m_Loaded = false;
    }

protected:
    virtual bool doLoad() = 0;
    virtual void doUnload() = 0;

    std::string m_ResourceId;       // Unique identifier for this resource within the system
private:
    bool m_Loaded = false;          // Loading state flag for resource lifecycle management
};

// Forward declaration
export template <typename T>
class ResourceHandle;

// Resource manager
export class ResourceManager {
public:
    ResourceManager() = default;

    template <typename T>
    ResourceHandle<T> load(const std::string & resourceNameOrPath) {
        static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

        // Check existing resource cache to avoid redundant loading
        auto type = std::type_index(typeid(T));
        auto & typeResources = m_Resources[type];
        auto it = typeResources.find(resourceNameOrPath);

        if (it != typeResources.end()) {
            // Resource exists in cache - increment reference count and return handle
            auto it2 = m_RefCounts.find(resourceNameOrPath);
            auto oldRefCount = it2->second;
            it2->second++;

            assert(it2->second > oldRefCount);
        }

        // Resource not found - create new resource instance and attempt loading
        std::shared_ptr<T> resource = std::make_shared<T>(resourceNameOrPath);
        if (!resource->load()) {
            // Loading failed - return invalid handle rather than corrupting cache
            return ResourceHandle<T>();
        }

        // Cache successful resource and initialize reference tracking
        typeResources[resourceNameOrPath] = resource;
        m_RefCounts[resourceNameOrPath] = 1;

        return ResourceHandle<T>(resource->getId(), this);
    }

    template <typename T>
    [[nodiscard]] T * getResource(const std::string & resourceId) {
        auto & typeResources = m_Resources[std::type_index(typeid(T))];
        auto it = typeResources.find(resourceId);

        if (it != typeResources.end()) {
            // Resource found - perform safe downcast and return typed pointer
            return static_cast<T *>(it->second.get());
        }

        // Resource not found - return null for safe handling by caller
        return nullptr;
    }

    template <typename T>
    [[nodiscard]] bool hasResource(const std::string & resourceId) {
        // efficient existence check without resource access overhead
        const auto & typeResources = m_Resources[std::type_index(typeid(T))];
        const auto resourceIt = typeResources.find(resourceId);

        return resourceIt != typeResources.end();
    }

    template <typename T>
    [[nodiscard]] uint32_t getResourceTypeCount() {
        auto & typeResources = m_Resources[std::type_index(typeid(T))];
        return typeResources.size();
    }

    void release(const std::string & resourceId) {
        // Locate reference count entry for this resource
        auto it = m_RefCounts.find(resourceId);
        if (it != m_RefCounts.end()) {
            it->second--;

            // Check if resource has no remaining references
            if (it->second <= 0) {
                for (auto & [type, typeResources] : m_Resources) {
                    auto resourceIt = typeResources.find(resourceId);
                    if (resourceIt != typeResources.end()) {
                        resourceIt->second->unload();           // Allow resource to clean up its data
                        typeResources.erase(resourceIt);     // Remove from cache
                        break;
                    }
                }

                m_RefCounts.erase(it);
            }
        }
    }

    void unloadAll() {
        // Emergency cleanup method for system shutdown or major state changes
        for (auto & [type, typeResources] : m_Resources) {
            for (auto & [id, resource] : typeResources) {
                resource->unload();
            }
            typeResources.clear();
        }
        m_RefCounts.clear();
    }

private:
    // Two-level storage system: organize by type first, then by unique identifier
    // This approach enables type-safe resource access while maintaining efficient lookup
    std::unordered_map<std::type_index, std::unordered_map<std::string, std::shared_ptr<Resource>>> m_Resources {};

    // Reference counting system for automatic resource lifecycle management
    std::unordered_map<std::string, int> m_RefCounts {};
};

// Resource handle
template <typename T>
class ResourceHandle {
public:
    ResourceHandle() : m_ResourceManager(nullptr) {}

    ResourceHandle(const std::string & id, ResourceManager * manager)
        : m_ResourceId(id), m_ResourceManager(manager) {}

    T * get() const {
        if (!m_ResourceManager) return nullptr;

        return m_ResourceManager->getResource<T>(m_ResourceId);
    }

    [[nodiscard]] bool isValid() const {
        return m_ResourceManager && m_ResourceManager->hasResource<T>(m_ResourceId);
    }

    [[nodiscard]] const std::string & getId() const {
        return m_ResourceId;
    }

    // Convenience operators
    T * operator->() const {
        return get();
    }

    T & operator*() const {
        return get();
    }

    explicit operator bool() const {
        return isValid();
    }

private:
    std::string m_ResourceId;
    ResourceManager * m_ResourceManager;
};

// Texture resource
export class Texture : public Resource {
public:
    explicit Texture(const std::string & path) {
        m_FileInfo = getFileInfo(path);
        m_ResourceId = m_FileInfo.name;
    }

    ~Texture() override {
        unload();
    }

    [[nodiscard]] vk::Image getImage() const {
        return m_Image;
    }

    [[nodiscard]] vk::ImageView getImageView() const {
        return m_ImageView;
    }

    // TODO some kind of global sampler?
    [[nodiscard]] vk::Sampler getSampler() const {
        return m_Sampler;
    }

    [[nodiscard]] uint32_t getIndex() const {
        return m_Index;
    }

    void setIndex(uint32_t index) {
        m_Index = index;
    }

protected:
    bool doLoad() override {
        // Should work for both PNGs and JPEGs, will expand as necessary
        StbImageWrapper data = loadImageData();
        if (!data.pixels) {
            return false;
        }

        createVulkanImage(data);

        return true;
    }

    void doUnload() override {
        // Only perform cleanup if resource is currently loaded
        if (isLoaded()) {
            VulkanResourceServiceLocator::locate()->freeResources(m_Image, m_DeviceMemory, m_ImageView, m_Sampler);
        }
    }

private:
    StbImageWrapper loadImageData() {
        // TODO expand for other formats like KTX, currently will only work with more traditional image formats
        auto imageData = StbImageWrapper(m_FileInfo);

        m_Width = imageData.width;
        m_Height = imageData.height;
        m_Channels = imageData.channels;
        return imageData;
    }

    void createVulkanImage(StbImageWrapper & data) {
        const auto imageData = VulkanResourceServiceLocator::locate()->createSampledImageTexture(data);

        m_Image = imageData.image;
        m_DeviceMemory = imageData.imageMemory;
        m_Offset = 0; // TODO: change when we start supporting different offsets
        m_ImageView = imageData.imageView;
    }

private:
    // Core Vulkan GPU resources for texture representation
    vk::Image m_Image = nullptr;                      // GPU image object containing pixel data
    vk::DeviceMemory m_DeviceMemory = nullptr;        // GPU memory allocation backing the image
    vk::DeviceSize m_Offset = 0;                            // Offset within the memory allocation for this texture
    vk::ImageView m_ImageView = nullptr;              // Shader-accessible view into the image
    vk::Sampler m_Sampler = nullptr;                  // Sampling configuration (filtering, wrapping, etc)

    // texture metadata for validation and debugging
    int m_Width = 0;                          // Image width in pixels
    int m_Height = 0;                         // Image height in pixels
    int m_Channels = 0;                       // Number of color channels (RGB=3, RGBA=4, etc)

    // Index of texture inside bindless descriptor set
    // Problem - can only bind texture to one pipeline at once
    // but that doesn't really matter for now (can extract elsewhere later)
    uint32_t m_Index = INDEX_UNSET;

    FileInfo m_FileInfo;
};

// A 3D asset which can either hold a singular simple mesh or a more complex scene graph structure
export class Asset3D : public Resource {
public:
    explicit Asset3D(const std::string & path) {
        m_FileInfo = getFileInfo(path);
        m_ResourceId = m_FileInfo.name;
    }

    // TODO rule of 5
    ~Asset3D() override {
        unload();
    }

    [[nodiscard]] vk::Buffer getVertexBuffer() const {
        return m_VertexBuffer;
    }

    [[nodiscard]] vk::Buffer getIndexBuffer() const {
        return m_IndexBuffer;
    }

    [[nodiscard]] uint32_t getVertexCount() const {
        return m_VertexCount;
    }

    [[nodiscard]] uint32_t getIndexCount() const {
        return m_IndexCount;
    }

protected:
    bool doLoad() override {
        // TODO support more formats (like glTF)

        // Temp storage for vertices and indices
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        if (!loadMeshData(vertices, indices)) {
            return false;   // Failed to parse - abort loading
        }

        createVertexBuffer(vertices);   // Upload vertex attributes to GPU
        createIndexBuffer(indices);    // Upload triangle connectivity to GPU

        // Cache metadata for efficient rendering operations
        m_VertexCount = static_cast<uint32_t>(vertices.size());
        m_IndexCount = static_cast<uint32_t>(indices.size());

        return true;
    }

    void doUnload() override {
        // Only proceed with cleanup if resources are currently loaded
        if (isLoaded()) {
            // Clean up index buffers first, vertex buffers second to maintain clear dependency order
            VulkanResourceServiceLocator::locate()->freeResources(m_IndexBuffer, m_IndexBufferMemory);
            VulkanResourceServiceLocator::locate()->freeResources(m_VertexBuffer, m_VertexBufferMemory);
        }
    }

private:
    bool loadMeshData(std::vector<Vertex> & vertices, std::vector<uint32_t> & indices) {
        if (m_FileInfo.extension == ".obj") {
            return loadObj(vertices, indices);
        } else if (m_FileInfo.extension == ".gltf") {
            return loadGltf(vertices, indices);
        } else {
            // Unsupported format
            return false;
        }
    }

    bool loadObj(std::vector<Vertex> & vertices, std::vector<uint32_t> & indices) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        // TODO support loading materials
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;
        std::unordered_map<Vertex, uint32_t> uniqueVertices {};

        // TODO wchar support?
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, m_FileInfo.absolutePath.string().c_str())) {
            std::println("Error loading OBJ model from path {}: {} {}", m_FileInfo.absolutePath.string(), warn, err);
            return false;
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

        return true;
    }

    bool loadGltf(std::vector<Vertex> & vertices, std::vector<uint32_t> & indices) {
        return false;
    }

    void createVertexBuffer(std::vector<Vertex> & vertices) {
        // TODO look into using memory barriers
        auto [buffer, bufferMemory] = VulkanResourceServiceLocator::locate()->createVulkanBuffer(
            sizeof(vertices[0]) * vertices.size(),
            vk::BufferUsageFlagBits::eVertexBuffer,
            vertices.data()
        );

        m_VertexBuffer = buffer;
        m_VertexBufferMemory = bufferMemory;
    }

    void createIndexBuffer(std::vector<uint32_t> & indices) {
        // TODO optimize memory allocation for read-heavy access patterns, add index format validation (16-bit vs 32-bit)
        auto [buffer, bufferMemory] = VulkanResourceServiceLocator::locate()->createVulkanBuffer(
            sizeof(indices[0]) * indices.size(),
            vk::BufferUsageFlagBits::eIndexBuffer,
            indices.data()
        );

        m_IndexBuffer = buffer;
        m_IndexBufferMemory = bufferMemory;
    }

private:
    std::string m_Extension;

    // If the asset is a simple mesh, it manages its own memory; otherwise, the underlying scene structure does it
    bool m_SimpleMesh = true;
    SceneGraphAsset * m_SceneGraph = nullptr;

    // Vertex data management - stores per-vertex attributes like position, normal, uv coords
    vk::Buffer m_VertexBuffer = nullptr;                    // GPU buffer containing vertex attribute data
    vk::DeviceMemory m_VertexBufferMemory = nullptr;        // GPU memory backing the vertex buffer
    vk::DeviceSize m_VertexBufferOffset = 0;                // Offset within the memory allocation for vertex buffer
    uint32_t m_VertexCount = 0;                             // Number of vertices in this mesh

    // Index data management - defines triangle connectivity using vertex indices
    vk::Buffer m_IndexBuffer = nullptr;                     // GPU buffer containing triangle index data
    vk::DeviceMemory m_IndexBufferMemory = nullptr;         // GPU memory backing the index buffer
    vk::DeviceSize m_IndexBufferOffset = 0;                 // Offset within the memory allocation for index buffer
    uint32_t m_IndexCount = 0;                              // Number of indices in this mesh (typically 3 per triangle)

    FileInfo m_FileInfo;
};

export class Material : public Resource {
public:
    explicit Material(const std::string & id, const glm::vec4 & materialTint = glm::vec4(1.0f))
        : m_MaterialTint(materialTint) {
        m_ResourceId = id;
    }

    // TODO rule of 5
    ~Material() override {
        unload();
    }

    [[nodiscard]] glm::vec4 getMaterialTint() const {
        return m_MaterialTint;
    }

    void setMaterialTint(const glm::vec4 & materialTint) {
        m_MaterialTint = materialTint;
        m_OutOfDate = true;
    }

    [[nodiscard]] Texture * getTexture() const {
        return m_Texture;
    }

    void setTexture(Texture * texture) {
        m_Texture = texture;
        m_OutOfDate = true;
    }

    [[nodiscard]] uint32_t getIndex() const {
        return m_MaterialIndex;
    }

    // returns true if device mappings changed, false otherwise
    bool update() {
        if (m_OutOfDate) {
            auto * materialPtr = DeviceMapperLocator::locate()->getMaterial(m_MaterialIndex);
            materialPtr->layout.materialTint = m_MaterialTint;
            materialPtr->layout.textureIndex = m_Texture ? m_Texture->getIndex() : INDEX_UNSET;

            m_OutOfDate = false;
            return true;
        }
        return false;
    }

protected:
    bool doLoad() override {
        const auto deviceMapper = DeviceMapperLocator::locate();
        assert(deviceMapper != nullptr);
        m_MaterialIndex = deviceMapper->addMaterial(m_MaterialTint, INDEX_UNSET);

        return true;
    }

    void doUnload() override {
        const auto deviceMapper = DeviceMapperLocator::locate();
        assert(deviceMapper != nullptr);
        deviceMapper->removeMaterial(m_MaterialIndex);
    }

private:
    bool m_OutOfDate = true;

    glm::vec4 m_MaterialTint;
    // TODO: keeping a raw pointer here seems incredibly fragile, probably change this
    Texture * m_Texture = nullptr;

    uint32_t m_MaterialIndex = INDEX_UNSET;
};
