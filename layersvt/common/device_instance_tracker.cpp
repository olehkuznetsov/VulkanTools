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

#include "device_instance_tracker.h"
#include <mutex>

#include <vector>

namespace layersvt {

DeviceInstanceTracker& DeviceInstanceTracker::Get() {
    static DeviceInstanceTracker instance;
    return instance;
}

void DeviceInstanceTracker::SetVkInstance(VkPhysicalDevice physical_device, VkInstance instance) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    physical_device_to_instance_map_[physical_device] = instance;
}

VkInstance DeviceInstanceTracker::GetVkInstance(VkPhysicalDevice physical_device) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = physical_device_to_instance_map_.find(physical_device);
    if (it != physical_device_to_instance_map_.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

void DeviceInstanceTracker::EagerMapDevices(VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr) {
    if (!instance || !get_instance_proc_addr) {
        return;
    }
    auto enumerate_physical_devices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_instance_proc_addr(instance, "vkEnumeratePhysicalDevices"));
    if (!enumerate_physical_devices) {
        return;
    }

    uint32_t count = 0;
    VkResult result = enumerate_physical_devices(instance, &count, nullptr);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && count > 0) {
        std::vector<VkPhysicalDevice> devices(count);
        result = enumerate_physical_devices(instance, &count, devices.data());
        if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            for (uint32_t i = 0; i < count; ++i) {
                physical_device_to_instance_map_[devices[i]] = instance;
            }
        }
    }
}

void DeviceInstanceTracker::RemoveInstance(VkInstance instance) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto it = physical_device_to_instance_map_.begin(); it != physical_device_to_instance_map_.end();) {
        if (it->second == instance) {
            it = physical_device_to_instance_map_.erase(it);
        } else {
            ++it;
        }
    }
}

void DeviceInstanceTracker::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    physical_device_to_instance_map_.clear();
}

}  // namespace layersvt
