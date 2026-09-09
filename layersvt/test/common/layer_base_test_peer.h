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

#include "common/layer_base.h"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace layersvt {

class LayerBaseTestPeer {
   public:
    static PFN_vkVoidFunction GetKnownInstanceCommand(const char* name) {
        return LayerBase::GetKnownInstanceCommand(name);
    }
    static PFN_vkVoidFunction GetKnownDeviceCommand(const char* name) {
        return LayerBase::GetKnownDeviceCommand(name);
    }

    static VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* physical_device_count,
                                             VkPhysicalDevice* physical_devices) {
        return LayerBase::EnumeratePhysicalDevices(instance, physical_device_count, physical_devices);
    }

    static VkResult EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* physical_device_group_count,
                                                  VkPhysicalDeviceGroupProperties* physical_device_group_properties) {
        return LayerBase::EnumeratePhysicalDeviceGroups(instance, physical_device_group_count, physical_device_group_properties);
    }
    static VkResult CreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                 const VkAllocationCallbacks* allocator, VkDevice* device) {
        return LayerBase::CreateDevice(physical_device, create_info, allocator, device);
    }

    static void DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
        LayerBase::DestroyDevice(device, allocator);
    }
    static DispatchTableManager& GetDispatchTableManager(LayerBase& layer) { return layer.GetDispatchTableManager(); }
    static const DispatchTableManager& GetDispatchTableManager(const LayerBase& layer) { return layer.GetDispatchTableManager(); }

    static VkInstance GetVkInstance(VkPhysicalDevice physical_device) { return LayerBase::GetVkInstance(physical_device); }

    static VkuInstanceDispatchTable* GetInstanceDispatchTable(VkInstance instance) {
        return LayerBase::GetInstanceDispatchTable(instance);
    }
    static VkuInstanceDispatchTable* GetInstanceDispatchTable(VkPhysicalDevice physical_device) {
        return LayerBase::GetInstanceDispatchTable(physical_device);
    }

    static VkuDeviceDispatchTable* GetDeviceDispatchTable(const void* object) { return LayerBase::GetDeviceDispatchTable(object); }

    static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* name) {
        return LayerBase::GetInstanceProcAddr(instance, name);
    }
    static PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* name) {
        return LayerBase::GetDeviceProcAddr(device, name);
    }

    static VkResult CreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator,
                                   VkInstance* instance) {
        return LayerBase::CreateInstance(create_info, allocator, instance);
    }

    static void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
        LayerBase::DestroyInstance(instance, allocator);
    }
};

}  // namespace layersvt
