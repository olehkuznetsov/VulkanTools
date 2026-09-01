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
    virtual ~LayerBase() = default;

    const LayerManifest* GetManifest() const { return manifest_; }

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
    const LayerManifest* manifest_ = nullptr;
};

}  // namespace layersvt
