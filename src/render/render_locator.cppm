module;

export module render_service_locator;

import vulkan_resource_service;

export class Locator {
public:
    static VulkanResourceService * getVulkanResourceService() {
        return vulkanResourceService;
    }

    static void provide(VulkanResourceService * service) {
        vulkanResourceService = service;
    }
private:
    static VulkanResourceService * vulkanResourceService;
};

VulkanResourceService * Locator::vulkanResourceService = nullptr;
