#include "VulkanDispatch.hpp"
#include "VulkanScene.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>
#include <string>

namespace c4::rendering {
namespace {
// ponytail: synchronous main-thread renderer entrypoints select their own table.
// The fallback keeps pointer guards safe before any Vulkan renderer is active.
VulkanDispatch fallback_dispatch{};

template <class Function>
Function required_function(PFN_vkVoidFunction address, const char* name) {
    if (!address) throw VulkanUnavailableError(std::string("Missing required Vulkan function: ") + name);
    return reinterpret_cast<Function>(address);
}
} // namespace

VulkanDispatch* c4_active_vulkan_dispatch = &fallback_dispatch;

VulkanDispatchScope::VulkanDispatchScope(VulkanDispatch& dispatch) noexcept
    : previous_(c4_active_vulkan_dispatch) {
    c4_active_vulkan_dispatch = &dispatch;
}

VulkanDispatchScope::~VulkanDispatchScope() {
    c4_active_vulkan_dispatch = previous_;
}

void load_vulkan_global_functions(VulkanDispatch& dispatch) {
    if (dispatch.library_loaded)
        throw std::runtime_error("Vulkan dispatch already owns a loader reference");
    // SDL refcounts successful loads, including the Vulkan window's reference.
    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw VulkanUnavailableError(std::string("Cannot load Vulkan library: ") + SDL_GetError());
    dispatch.library_loaded = true;
    try {
        dispatch.c4_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!dispatch.c4_vkGetInstanceProcAddr)
            throw VulkanUnavailableError("SDL Vulkan loader missing vkGetInstanceProcAddr");
#define C4_VK_LOAD_GLOBAL(name) \
        dispatch.c4_vk##name = required_function<PFN_vk##name>(dispatch.c4_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name), "vk" #name);
        C4_VK_REQUIRED_GLOBAL_FUNCTIONS(C4_VK_LOAD_GLOBAL)
#undef C4_VK_LOAD_GLOBAL
#define C4_VK_LOAD_OPTIONAL_GLOBAL(name) \
        dispatch.c4_vk##name = reinterpret_cast<PFN_vk##name>(dispatch.c4_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name));
        C4_VK_OPTIONAL_GLOBAL_FUNCTIONS(C4_VK_LOAD_OPTIONAL_GLOBAL)
#undef C4_VK_LOAD_OPTIONAL_GLOBAL
    } catch (...) {
        unload_vulkan_library(dispatch);
        throw;
    }
}

void load_vulkan_instance_functions(VulkanDispatch& dispatch, VkInstance instance) {
    if (!dispatch.library_loaded || !dispatch.c4_vkGetInstanceProcAddr || !instance)
        throw std::runtime_error("Vulkan instance dispatch requires a loaded library and an instance");
    // DestroyInstance is first so any later lookup failure can unwind creation.
#define C4_VK_LOAD_INSTANCE(name) \
    dispatch.c4_vk##name = required_function<PFN_vk##name>(dispatch.c4_vkGetInstanceProcAddr(instance, "vk" #name), "vk" #name);
    C4_VK_REQUIRED_INSTANCE_FUNCTIONS(C4_VK_LOAD_INSTANCE)
#undef C4_VK_LOAD_INSTANCE
    // Disabled debug utils must never prevent Original/raster initialization.
    // An owner enabling the extension must check both pointers before creation.
#define C4_VK_LOAD_OPTIONAL_INSTANCE(name) \
    dispatch.c4_vk##name = reinterpret_cast<PFN_vk##name>(dispatch.c4_vkGetInstanceProcAddr(instance, "vk" #name));
    C4_VK_OPTIONAL_INSTANCE_FUNCTIONS(C4_VK_LOAD_OPTIONAL_INSTANCE)
#undef C4_VK_LOAD_OPTIONAL_INSTANCE
}

void load_vulkan_device_functions(VulkanDispatch& dispatch, VkDevice device, bool rt_enabled) {
    if (!dispatch.library_loaded || !dispatch.c4_vkGetDeviceProcAddr || !device)
        throw std::runtime_error("Vulkan device dispatch requires an initialized instance and a device");
    // DestroyDevice and DeviceWaitIdle precede all helper/resource entrypoints.
#define C4_VK_LOAD_DEVICE(name) \
    dispatch.c4_vk##name = required_function<PFN_vk##name>(dispatch.c4_vkGetDeviceProcAddr(device, "vk" #name), "vk" #name);
    C4_VK_REQUIRED_DEVICE_FUNCTIONS(C4_VK_LOAD_DEVICE)
    if (rt_enabled) {
        C4_VK_RAY_QUERY_FUNCTIONS(C4_VK_LOAD_DEVICE)
    } else {
#define C4_VK_CLEAR_RAY_QUERY(name) dispatch.c4_vk##name = nullptr;
        C4_VK_RAY_QUERY_FUNCTIONS(C4_VK_CLEAR_RAY_QUERY)
#undef C4_VK_CLEAR_RAY_QUERY
    }
#undef C4_VK_LOAD_DEVICE
}

void unload_vulkan_library(VulkanDispatch& dispatch) noexcept {
    // The owner has already destroyed every object that may call its PFNs.
#define C4_VK_CLEAR(name) dispatch.c4_vk##name = nullptr;
    C4_VK_REQUIRED_GLOBAL_FUNCTIONS(C4_VK_CLEAR)
    C4_VK_OPTIONAL_GLOBAL_FUNCTIONS(C4_VK_CLEAR)
    C4_VK_REQUIRED_INSTANCE_FUNCTIONS(C4_VK_CLEAR)
    C4_VK_OPTIONAL_INSTANCE_FUNCTIONS(C4_VK_CLEAR)
    C4_VK_REQUIRED_DEVICE_FUNCTIONS(C4_VK_CLEAR)
    C4_VK_RAY_QUERY_FUNCTIONS(C4_VK_CLEAR)
#undef C4_VK_CLEAR
    dispatch.c4_vkGetInstanceProcAddr = nullptr;
    if (dispatch.library_loaded) {
        dispatch.library_loaded = false;
        SDL_Vulkan_UnloadLibrary();
    }
}
} // namespace c4::rendering
