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
#include <mutex>
#include <cassert>

namespace layersvt {

DispatchTableManager& DispatchTableManager::Get() {
    static DispatchTableManager instance;
    return instance;
}

DispatchTableManager::~DispatchTableManager() { Clear(); }

void DispatchTableManager::Clear() {
    {
        std::unique_lock<std::shared_mutex> lock(instance_mutex_);
        instance_tables_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(device_mutex_);
        device_tables_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(callback_mutex_);
        loader_callbacks_.clear();
    }
}

VkuInstanceDispatchTable* DispatchTableManager::InitInstanceTable(VkInstance instance,
                                                                  PFN_vkGetInstanceProcAddr get_instance_proc_addr) {
    auto table = std::make_unique<VkuInstanceDispatchTable>();
    vkuInitInstanceDispatchTable(instance, table.get(), get_instance_proc_addr);

    DispatchKey key = GetDispatchKey(instance);
    std::unique_lock<std::shared_mutex> lock(instance_mutex_);
    auto raw_ptr = table.get();
    instance_tables_[key] = std::move(table);
    return raw_ptr;
}

VkuInstanceDispatchTable* DispatchTableManager::GetInstanceDispatchTable(const void* object) const {
    DispatchKey key = GetDispatchKey(object);
    std::shared_lock<std::shared_mutex> lock(instance_mutex_);
    auto it = instance_tables_.find(key);
    if (it != instance_tables_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void DispatchTableManager::DestroyInstanceTable(DispatchKey key) {
    std::unique_lock<std::shared_mutex> lock(instance_mutex_);
    instance_tables_.erase(key);
}

VkuDeviceDispatchTable* DispatchTableManager::InitDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr) {
    auto table = std::make_unique<VkuDeviceDispatchTable>();
    vkuInitDeviceDispatchTable(device, table.get(), get_device_proc_addr);

    DispatchKey key = GetDispatchKey(device);
    std::unique_lock<std::shared_mutex> lock(device_mutex_);
    auto raw_ptr = table.get();
    device_tables_[key] = std::move(table);
    return raw_ptr;
}

VkuDeviceDispatchTable* DispatchTableManager::GetDeviceDispatchTable(const void* object) const {
    DispatchKey key = GetDispatchKey(object);
    std::shared_lock<std::shared_mutex> lock(device_mutex_);
    auto it = device_tables_.find(key);
    if (it != device_tables_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void DispatchTableManager::DestroyDeviceTable(DispatchKey key) {
    {
        std::unique_lock<std::shared_mutex> lock(device_mutex_);
        device_tables_.erase(key);
    }
    {
        std::unique_lock<std::shared_mutex> lock(callback_mutex_);
        loader_callbacks_.erase(key);
    }
}

void DispatchTableManager::SetDeviceLoaderDataCallback(VkDevice device, PFN_vkSetDeviceLoaderData callback) {
    DispatchKey key = GetDispatchKey(device);
    std::unique_lock<std::shared_mutex> lock(callback_mutex_);
    loader_callbacks_[key] = callback;
}

PFN_vkSetDeviceLoaderData DispatchTableManager::GetDeviceLoaderDataCallback(VkDevice device) const {
    DispatchKey key = GetDispatchKey(device);
    std::shared_lock<std::shared_mutex> lock(callback_mutex_);
    auto it = loader_callbacks_.find(key);
    if (it != loader_callbacks_.end()) {
        return it->second;
    }
    return nullptr;
}

VkLayerInstanceCreateInfo* GetChainInfo(const VkInstanceCreateInfo* create_info, VkLayerFunction function) {
    if (!create_info) {
        return nullptr;
    }
    auto* chain_info = static_cast<const VkLayerInstanceCreateInfo*>(create_info->pNext);
    while (chain_info && (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain_info->function != function)) {
        chain_info = static_cast<const VkLayerInstanceCreateInfo*>(chain_info->pNext);
    }
    return const_cast<VkLayerInstanceCreateInfo*>(chain_info);
}

VkLayerDeviceCreateInfo* GetChainInfo(const VkDeviceCreateInfo* create_info, VkLayerFunction function) {
    if (!create_info) {
        return nullptr;
    }
    auto* chain_info = static_cast<const VkLayerDeviceCreateInfo*>(create_info->pNext);
    while (chain_info && (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain_info->function != function)) {
        chain_info = static_cast<const VkLayerDeviceCreateInfo*>(chain_info->pNext);
    }
    return const_cast<VkLayerDeviceCreateInfo*>(chain_info);
}

}  // namespace layersvt
