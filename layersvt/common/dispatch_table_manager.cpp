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

#include "dispatch_table_manager.h"
#include <algorithm>
#include <cassert>
#include <mutex>
#include <vector>

namespace layersvt {

VkuInstanceDispatchTable* DispatchTableManager::InitInstanceTable(VkInstance instance,
                                                                  PFN_vkGetInstanceProcAddr get_instance_proc_addr) {
    assert(instance != VK_NULL_HANDLE);
    assert(get_instance_proc_addr != nullptr);

    Key key = GetDispatchKey(instance);
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto [iterator, inserted] = instances_.try_emplace(key);
    iterator->second.instance = instance;
    vkuInitInstanceDispatchTable(instance, &iterator->second.table, get_instance_proc_addr);
    return &iterator->second.table;
}

VkuInstanceDispatchTable* DispatchTableManager::GetInstanceDispatchTable(VkInstance instance) const {
    if (instance == VK_NULL_HANDLE) {
        return nullptr;
    }
    Key key = GetDispatchKey(instance);
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto table_iterator = instances_.find(key);
    if (table_iterator != instances_.end()) {
        return const_cast<VkuInstanceDispatchTable*>(&table_iterator->second.table);
    }
    return nullptr;
}

VkuInstanceDispatchTable* DispatchTableManager::GetInstanceDispatchTable(VkPhysicalDevice physical_device) const {
    if (physical_device == VK_NULL_HANDLE) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto device_iterator = physical_device_to_instance_map_.find(physical_device);
    if (device_iterator != physical_device_to_instance_map_.end()) {
        Key instance_key = GetDispatchKey(device_iterator->second);
        auto table_iterator = instances_.find(instance_key);
        if (table_iterator != instances_.end()) {
            return const_cast<VkuInstanceDispatchTable*>(&table_iterator->second.table);
        }
    }
    return nullptr;
}

void DispatchTableManager::DestroyInstanceTable(Key key) {
    assert(key != Key{});
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto iterator = instances_.find(key);
    if (iterator != instances_.end()) {
        VkInstance instance = iterator->second.instance;
        std::erase_if(physical_device_to_instance_map_,
                      [instance](const auto& entry) { return entry.second == instance; });
        instances_.erase(iterator);
    }
}

void DispatchTableManager::RegisterPhysicalDevices(const VkPhysicalDevice* physical_devices, uint32_t count,
                                                   VkInstance instance) {
    if (physical_devices == nullptr || count == 0) {
        return;
    }
    assert(instance != VK_NULL_HANDLE);
    std::lock_guard<std::mutex> lock(instance_mutex_);
    for (uint32_t i = 0; i < count; ++i) {
        assert(physical_devices[i] != VK_NULL_HANDLE);
        physical_device_to_instance_map_[physical_devices[i]] = instance;
    }
}

VkInstance DispatchTableManager::GetVkInstance(VkPhysicalDevice physical_device) const {
    if (physical_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto device_iterator = physical_device_to_instance_map_.find(physical_device);
    if (device_iterator != physical_device_to_instance_map_.end()) {
        return device_iterator->second;
    }
    return VK_NULL_HANDLE;
}

VkuDeviceDispatchTable* DispatchTableManager::InitDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr) {
    assert(device != VK_NULL_HANDLE);
    assert(get_device_proc_addr != nullptr);

    Key key = GetDispatchKey(device);
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto [iterator, inserted] = device_entries_.try_emplace(key);
    vkuInitDeviceDispatchTable(device, &iterator->second.table, get_device_proc_addr);
    return &iterator->second.table;
}

VkuDeviceDispatchTable* DispatchTableManager::GetDeviceDispatchTable(const void* object) const {
    if (object == nullptr) {
        return nullptr;
    }
    Key key = GetDispatchKey(object);
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto table_iterator = device_entries_.find(key);
    if (table_iterator != device_entries_.end()) {
        return const_cast<VkuDeviceDispatchTable*>(&table_iterator->second.table);
    }
    return nullptr;
}

void DispatchTableManager::DestroyDeviceTable(Key key) {
    assert(key != Key{});
    std::lock_guard<std::mutex> lock(device_mutex_);
    device_entries_.erase(key);
}

void DispatchTableManager::SetDeviceLoaderDataCallback(VkDevice device, PFN_vkSetDeviceLoaderData callback) {
    assert(device != VK_NULL_HANDLE);
    assert(callback != nullptr);
    Key key = GetDispatchKey(device);
    std::lock_guard<std::mutex> lock(device_mutex_);
    device_entries_[key].loader_callback = callback;
}

PFN_vkSetDeviceLoaderData DispatchTableManager::GetDeviceLoaderDataCallback(VkDevice device) const {
    assert(device != VK_NULL_HANDLE);
    Key key = GetDispatchKey(device);
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto callback_iterator = device_entries_.find(key);
    if (callback_iterator != device_entries_.end()) {
        return callback_iterator->second.loader_callback;
    }
    return nullptr;
}

}  // namespace layersvt
