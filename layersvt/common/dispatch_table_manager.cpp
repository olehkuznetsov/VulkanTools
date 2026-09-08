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
    auto table = std::make_unique<VkuInstanceDispatchTable>();
    vkuInitInstanceDispatchTable(instance, table.get(), get_instance_proc_addr);

    Key key = GetDispatchKey(instance);
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto [iterator, inserted] = instance_tables_.try_emplace(key, std::move(table));
    instance_keys_[key] = instance;
    return iterator->second.get();
}

VkuInstanceDispatchTable* DispatchTableManager::GetInstanceDispatchTable(VkInstance instance) const {
    if (instance == VK_NULL_HANDLE) {
        return nullptr;
    }
    Key key = GetDispatchKey(instance);
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto table_iterator = instance_tables_.find(key);
    if (table_iterator != instance_tables_.end()) {
        return table_iterator->second.get();
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
        auto table_iterator = instance_tables_.find(instance_key);
        if (table_iterator != instance_tables_.end()) {
            return table_iterator->second.get();
        }
    }
    return nullptr;
}

void DispatchTableManager::DestroyInstanceTable(Key key) {
    assert(key != Key{});
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto key_iterator = instance_keys_.find(key);
    if (key_iterator != instance_keys_.end()) {
        VkInstance instance = key_iterator->second;
        std::erase_if(physical_device_to_instance_map_,
                      [instance](const auto& entry) { return entry.second == instance; });
        instance_keys_.erase(key_iterator);
    }
    instance_tables_.erase(key);
}

void DispatchTableManager::SetVkInstance(VkPhysicalDevice physical_device, VkInstance instance) {
    assert(physical_device != VK_NULL_HANDLE);
    assert(instance != VK_NULL_HANDLE);
    std::lock_guard<std::mutex> lock(instance_mutex_);
    physical_device_to_instance_map_[physical_device] = instance;
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
    auto table = std::make_unique<VkuDeviceDispatchTable>();
    vkuInitDeviceDispatchTable(device, table.get(), get_device_proc_addr);

    Key key = GetDispatchKey(device);
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto [iterator, inserted] = device_tables_.try_emplace(key, std::move(table));
    return iterator->second.get();
}

VkuDeviceDispatchTable* DispatchTableManager::GetDeviceDispatchTable(const void* object) const {
    if (object == nullptr) {
        return nullptr;
    }
    Key key = GetDispatchKey(object);
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto table_iterator = device_tables_.find(key);
    if (table_iterator != device_tables_.end()) {
        return table_iterator->second.get();
    }
    return nullptr;
}

void DispatchTableManager::DestroyDeviceTable(Key key) {
    assert(key != Key{});
    std::lock_guard<std::mutex> lock(device_mutex_);
    device_tables_.erase(key);
    loader_callbacks_.erase(key);
}

void DispatchTableManager::SetDeviceLoaderDataCallback(VkDevice device, PFN_vkSetDeviceLoaderData callback) {
    assert(device != VK_NULL_HANDLE);
    assert(callback != nullptr);
    Key key = GetDispatchKey(device);
    std::lock_guard<std::mutex> lock(device_mutex_);
    loader_callbacks_[key] = callback;
}

PFN_vkSetDeviceLoaderData DispatchTableManager::GetDeviceLoaderDataCallback(VkDevice device) const {
    assert(device != VK_NULL_HANDLE);
    Key key = GetDispatchKey(device);
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto callback_iterator = loader_callbacks_.find(key);
    if (callback_iterator != loader_callbacks_.end()) {
        return callback_iterator->second;
    }
    return nullptr;
}

}  // namespace layersvt
