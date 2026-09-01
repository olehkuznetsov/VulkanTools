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
#include <shared_mutex>
#include <unordered_map>

namespace layersvt {

/**
 * @brief Thread-safe manager for tracking associations between Vulkan physical
 * devices and their corresponding parent instances.
 *
 * Vulkan layers require the VkInstance handle to resolve downstream function
 * pointers in vkCreateDevice via pfnNextGetInstanceProcAddr. This class provides
 * synchronized, low-latency lookups using a reader-writer lock (std::shared_mutex).
 */
class DeviceInstanceTracker {
   public:
    static DeviceInstanceTracker& Get();

    /**
     * @brief Associates a physical device handle with its parent VkInstance.
     */
    void SetVkInstance(VkPhysicalDevice physical_device, VkInstance instance);

    /**
     * @brief Retrieves the VkInstance associated with a given physical device.
     * @return The associated VkInstance, or VK_NULL_HANDLE if not registered.
     */
    VkInstance GetVkInstance(VkPhysicalDevice physical_device) const;

    /**
     * @brief Eagerly enumerates and associates all physical devices under an instance.
     *
     * Calling this during vkCreateInstance guarantees that all physical devices
     * are mapped to their parent instance before any vkCreateDevice call, even if
     * the application acquires physical devices prior to layer hooking or across threads.
     */
    void EagerMapDevices(VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr);

    /**
     * @brief Removes all physical device mappings associated with a specific VkInstance.
     */
    void RemoveInstance(VkInstance instance);

    /**
     * @brief Clears all registered mappings.
     */
    void Clear();

   private:
    DeviceInstanceTracker() = default;
    ~DeviceInstanceTracker() = default;
    DeviceInstanceTracker(const DeviceInstanceTracker&) = delete;
    DeviceInstanceTracker& operator=(const DeviceInstanceTracker&) = delete;

    mutable std::shared_mutex mutex_;
    std::unordered_map<VkPhysicalDevice, VkInstance> physical_device_to_instance_map_;
};

}  // namespace layersvt
