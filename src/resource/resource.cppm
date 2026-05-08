module;

#include <cassert>
#include <typeindex>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

export module resource;

import std;
import platform;
import render_service_locator;
import render_types;

import tinyobjloader;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

// Resource base class
export class Resource {
public:
    explicit Resource(const std::string & id) : m_ResourceId(id) {}
    virtual ~Resource() = default;

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
private:
    std::string m_ResourceId;       // Unique identifier for this resource within the system
    bool m_Loaded = false;          // Loading state flag for resource lifecycle management
};

// Forward declaration
export template<typename T>
class ResourceHandle;

// Resource manager
export class ResourceManager {
public:
    ResourceManager() = default;

    template<typename T>
    ResourceHandle<T> load(const std::string & resourceId) {
        static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

        // Check existing resource cache to avoid redundant loading
        auto type = std::type_index(typeid(T));
        auto & typeResources = m_Resources[type];
        auto it = typeResources.find(resourceId);

        if (it != typeResources.end()) {
            // Resource exists in cache - increment reference count and return handle
            auto it2 = m_RefCounts.find(resourceId);
            auto oldRefCount = it2->second;
            it2->second++;

            // TODO remove this assertion
            assert(it2->second > oldRefCount);
        }

        // Resource not found - create new resource instance and attempt loading
        std::shared_ptr<T> resource = std::make_shared<T>(resourceId);
        if (!resource->load()) {
            // Loading failed - return invalid handle rather than corrupting cache
            return ResourceHandle<T>();
        }

        // Cache successful resource and initialize reference tracking
        typeResources[resourceId] = resource;
        m_RefCounts[resourceId] = 1;

        return ResourceHandle<T>(resourceId, this);
    }

    template<typename T>
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

    template<typename T>
    [[nodiscard]] bool hasResource(const std::string & resourceId) {
        // efficient existence check without resource access overhead
        const auto & typeResources = m_Resources[std::type_index(typeid(T))];
        const auto resourceIt = typeResources.find(resourceId);

        return resourceIt != typeResources.end();
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
template<typename T>
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
    explicit Texture(const std::string & id) : Resource(id) {}

    // TODO rule of 5
    ~Texture() override {
        unload();
    }

    [[nodiscard]] vk::Image getImage() const {
        return m_Image;
    }

    [[nodiscard]] vk::ImageView getImageView() const {
        return m_ImageView;
    }

    [[nodiscard]] vk::Sampler getSampler() const {
        return m_Sampler;
    }
protected:
    bool doLoad() override {
        const std::string filePath = "assets/" + getId() + ".png";

        StbImageWrapper data = loadImageData(filePath);
        if (!data.pixels) {
            return false;
        }

        createVulkanImage(data);

        // StbImageWrapper's data will be freed automatically upon the object going out of scope
        return true;
    }

    void doUnload() override {
        // Only perform cleanup if resource is currently loaded
        if (isLoaded()) {
            const vk::Device device = Locator::getVulkanResourceService()->getDevice();

            device.destroySampler(m_Sampler);
            device.destroyImageView(m_ImageView);
            device.destroyImage(m_Image);
            device.freeMemory(m_DeviceMemory);
        }
    }
private:
    StbImageWrapper loadImageData(const std::string & filePath) {
        // TODO expand for other formats like KTX, currently will probably only work with more traditional image formats
        auto imageData = StbImageWrapper(filePath);

        m_Width = imageData.width;
        m_Height = imageData.height;
        m_Channels = imageData.channels;
        return imageData;
    }

    void createVulkanImage(StbImageWrapper & data) {
        const auto imageData = Locator::getVulkanResourceService()->createTexture(data);

        m_Image = imageData.image;
        m_DeviceMemory = imageData.imageMemory;
        m_Offset = 0; // TODO: change when we start supporting different offsets
        m_ImageView = imageData.imageView;
        m_Sampler = imageData.sampler;
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
};

export class Mesh : public Resource {
public:
    explicit Mesh(const std::string & id) : Resource(id) {}

    // TODO rule of 5
    ~Mesh() override {
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
        const std::string filePath = "assets/" + getId() + ".obj";

        // Temp storage for vertices and indices
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        if (!loadMeshData(filePath, vertices, indices)) {
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
            vk::Device device = Locator::getVulkanResourceService()->getDevice();

            // Clean up index resources first to maintain clear dependency order
            device.destroyBuffer(m_IndexBuffer);
            device.freeMemory(m_IndexBufferMemory);

            // Vertex resources cleaned up second
            device.destroyBuffer(m_VertexBuffer);
            device.freeMemory(m_VertexBufferMemory);
        }
    }
private:
    static bool loadMeshData(const std::string & filePath, std::vector<Vertex> & vertices, std::vector<uint32_t> & indices) {
        // TODO switch to tinygltf
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;
        std::unordered_map<Vertex, uint32_t> uniqueVertices {};

        // TODO wchar support?
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, pathFromProjectDir(filePath).string().c_str())) {
            std::print("Error loading model from path {}: {} {}", pathFromProjectDir(filePath).string(), warn, err);
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
                vertex.color = {1.0f, 1.0f, 1.0f};

                if (!uniqueVertices.contains(vertex)) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
        }

        return true;
    }

    void createVertexBuffer(std::vector<Vertex> & vertices) {
        // TODO look into using memory barriers
        auto [buffer, bufferMemory] = Locator::getVulkanResourceService()->createVulkanBuffer(
            sizeof(vertices[0]) * vertices.size(),
            vk::BufferUsageFlagBits::eVertexBuffer,
            vertices
        );

        m_VertexBuffer = buffer;
        m_VertexBufferMemory = bufferMemory;
    }

    void createIndexBuffer(std::vector<uint32_t> & indices) {
        // TODO optimize memory allocation for read-heavy access patterns, add index format validation (16-bit vs 32-bit)
        auto [buffer, bufferMemory] = Locator::getVulkanResourceService()->createVulkanBuffer(
            sizeof(indices[0]) * indices.size(),
            vk::BufferUsageFlagBits::eIndexBuffer,
            indices
        );

        m_IndexBuffer = buffer;
        m_IndexBufferMemory = bufferMemory;
    }
private:
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
};

// Shader resource
// TODO finish and use (not fully ready for use yet - slang-compiled shaders with multiple entry points won't work)
export class Shader : public Resource {
public:
    Shader(const std::string & id, vk::ShaderStageFlagBits shaderStage)
        : Resource(id), m_Stage(shaderStage) {}

    ~Shader() override {
        unload();
    }

    [[nodiscard]] vk::ShaderModule getShaderModule() const {
        return m_ShaderModule;
    }

    [[nodiscard]] vk::ShaderStageFlagBits getStage() const {
        return m_Stage;
    }
protected:
    bool doLoad() override {
        // Determine file extension based on shader stage
        std::string extension;
        switch (m_Stage) {
            case vk::ShaderStageFlagBits::eVertex: extension = ".vert"; break;
            case vk::ShaderStageFlagBits::eFragment: extension = ".frag"; break;
            case vk::ShaderStageFlagBits::eCompute: extension = ".comp"; break;
            // case vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment: extension = ""; break;
            default: return false;
        }

        // Load shader from file
        std::string filePath = "shaders/" + getId() + extension + ".spv";

        // Read shader code
        std::vector<char> shaderCode = readFile(filePath);

        createShaderModule(shaderCode);

        return true;
    }

    void doUnload() override {
        if (isLoaded()) {
            vk::Device device = Locator::getVulkanResourceService()->getDevice();

            device.destroyShaderModule(m_ShaderModule);
        }
    }
private:
    void createShaderModule(const std::vector<char> & shaderCode) {
        m_ShaderModule = Locator::getVulkanResourceService()->createShaderModule(shaderCode);
    }
private:
    vk::ShaderModule m_ShaderModule = nullptr;
    vk::ShaderStageFlagBits m_Stage{};
};

// TODO
export class Material {
public:

private:
};
