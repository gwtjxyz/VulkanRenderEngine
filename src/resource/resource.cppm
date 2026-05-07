module;

#include <cassert>
#include <typeindex>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

export module resource;

import std;
import platform;
import render_service_locator;

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
        std::string filePath = "assets/" + getId() + ".png";

        StbImageWrapper data = loadImageData(filePath);
        if (!data.pixels) {
            return false;
        }

        createVulkanImage(data);

        return true;
    }

    void doUnload() override {
        // Only perform cleanup if resource is currently loaded
        if (isLoaded()) {
            vk::Device device = getDevice();

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
        auto imageData = Locator::getVulkanResourceService()->createTexture(data);

        m_Image = imageData.image;
        m_DeviceMemory = imageData.imageMemory;
        m_Offset = 0; // TODO: change when we start supporting different offsets
        m_ImageView = imageData.imageView;
        m_Sampler = imageData.sampler;
    }

    vk::Device getDevice() {
        return Locator::getVulkanResourceService()->getDevice();
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

    // Render resource service locator
};
