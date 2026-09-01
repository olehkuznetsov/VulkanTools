/* Copyright (C) 2026 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

#ifndef VK_LAYER_EXPORT
#if defined(_WIN32)
#define VK_LAYER_EXPORT __declspec(dllexport)
#else
#define VK_LAYER_EXPORT __attribute__((visibility("default")))
#endif
#endif

/**
 * @brief Macro to define standard Vulkan layer exported C entry points (vkGetInstanceProcAddr
 * and vkGetDeviceProcAddr) for a LayerBase-derived singleton class.
 *
 * Instantiate this macro in the layer's dispatch translation unit (compiled only into the
 * layer shared library module, avoiding symbol collisions in test executables).
 */
#define DEFINE_VK_LAYER_ENTRYPOINTS(LayerClass)                                                     \
    extern "C" {                                                                                    \
    VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(                 \
        VkInstance instance, const char* pName) {                                                   \
        return LayerClass::Get().GetInstanceProcAddr(instance, pName);                              \
    }                                                                                               \
    VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(                   \
        VkDevice device, const char* pName) {                                                       \
        return LayerClass::Get().GetDeviceProcAddr(device, pName);                                  \
    }                                                                                               \
    VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(               \
        uint32_t* pPropertyCount, VkLayerProperties* pProperties) {                                 \
        return LayerClass::Get().EnumerateInstanceLayerProperties(pPropertyCount, pProperties);      \
    }                                                                                               \
    VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(           \
        const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {      \
        return LayerClass::Get().EnumerateInstanceExtensionProperties(                              \
            pLayerName, pPropertyCount, pProperties);                                              \
    }                                                                                               \
    VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(                  \
        VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) { \
        return LayerClass::Get().EnumerateDeviceLayerProperties(                                    \
            physicalDevice, pPropertyCount, pProperties);                                           \
    }                                                                                               \
    VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(              \
        VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount,          \
        VkExtensionProperties* pProperties) {                                                       \
        return LayerClass::Get().EnumerateDeviceExtensionProperties(                                \
            physicalDevice, pLayerName, pPropertyCount, pProperties);                               \
    }                                                                                               \
    }

namespace layersvt {

class LayerManifest;

/**
 * @brief Base class for Vulkan layers providing standard lifecycle management,
 * loader chain unwrapping, and dispatch table tracking.
 *
 * Derived classes can customize behavior by overriding the virtual hooks:
 * - PreCreateInstance / PostCreateInstance
 * - PreDestroyInstance / PostDestroyInstance
 * - PreCreateDevice / OnDeviceCreated
 * - PreDestroyDevice / PostDestroyDevice
 */
class LayerBase {
   public:
    explicit LayerBase(const LayerManifest* manifest = nullptr);
    virtual ~LayerBase();

    const LayerManifest* GetManifest() const { return manifest_; }

    // === Active Layer Management ===
    static LayerBase* GetActiveLayer() { return active_layer_; }
    static void SetActiveLayer(LayerBase* layer) { active_layer_ = layer; }

    // === Vulkan ProcAddr Dispatch (Template Method / Non-Virtual Interface) ===

    PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* name);
    PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* name);

    PFN_vkVoidFunction GetKnownInstanceFunction(const char* name);
    PFN_vkVoidFunction GetKnownDeviceFunction(const char* name);

    // === Core Vulkan Lifecycle Intercepts ===

    virtual VkResult CreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator,
                                    VkInstance* instance);

    virtual void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator);

    virtual VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* physical_device_count,
                                              VkPhysicalDevice* physical_devices);

    virtual VkResult EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* physical_device_group_count,
                                                   VkPhysicalDeviceGroupProperties* physical_device_group_properties);

    virtual VkResult CreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                  const VkAllocationCallbacks* allocator, VkDevice* device);

    virtual void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);

    // === Vulkan Layer Manifest Enumeration ===

    VkResult EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                  VkExtensionProperties* properties);
    VkResult EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties);
    VkResult EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                            VkLayerProperties* properties);
    VkResult EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                uint32_t* property_count, VkExtensionProperties* properties);

    // === Customization Hooks ===

    virtual void PreCreateInstance(VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator);
    virtual void PostCreateInstance(VkInstance instance, const VkInstanceCreateInfo* create_info);
    virtual void PreDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator);
    virtual void PostDestroyInstance(VkInstance instance);

    virtual void PreCreateDevice(VkPhysicalDevice physical_device, VkDeviceCreateInfo* create_info,
                                 const VkAllocationCallbacks* allocator);
    virtual void OnDeviceCreated(VkDevice device, VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info);
    virtual void PreDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);
    virtual void PostDestroyDevice(VkDevice device);

   protected:
    // Hooks for layer-specific functions ONLY. Default returns nullptr.
    // Derived layers override these to expose layer-specific commands and extensions.
    virtual PFN_vkVoidFunction GetLayerSpecificInstanceFunction(const char* name);
    virtual PFN_vkVoidFunction GetLayerSpecificDeviceFunction(const char* name);

    const LayerManifest* manifest_ = nullptr;
    static inline LayerBase* active_layer_ = nullptr;
};

}  // namespace layersvt
