// This source file is only needed if compiling without vulkan-hpp module enabled
// Otherwise, vulkan-hpp module handles creation of a default loader automatically
//
// We also have to have this in a plain old source file instead of a module unit
// because the loader definition needs to be known to all other source/module files in the project
#if defined(DISABLE_VULKAN_MODULE) && defined(VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)

#include <vulkan/vulkan.hpp>

// If we're not using the Vulkan module, we need to define the dynamic dispatch loader and provide storage for it here
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
// vk::detail::DispatchLoaderDynamic defaultDispatchLoaderDynamic;

#endif