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

#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>
#include <shared_mutex>
#include <unordered_map>
#include <memory>
#include <type_traits>
#include <utility>

namespace layersvt {

using DispatchKey = void*;

/**
 * @brief Computes the dispatch key for a dispatchable Vulkan handle.
 *
 * In the Vulkan loader architecture, dispatchable handles (VkInstance,
 * VkPhysicalDevice, VkDevice, VkQueue, VkCommandBuffer) have their first
 * pointer-sized word pointing to the loader's dispatch table.
 */
inline DispatchKey GetDispatchKey(const void* object) {
    return (object == nullptr) ? nullptr : *reinterpret_cast<void* const*>(object);
}

/**
 * @brief Thread-safe manager for Vulkan instance and device dispatch tables.
 *
 * Provides thread-safe storage, initialization, lookup, and destruction of
 * dispatch tables using reader-writer locks (std::shared_mutex).
 *
 * Also tracks the loader data callback (VK_LOADER_DATA_CALLBACK) to allow
 * layers to initialize dispatchable objects created internally.
 */
class DispatchTableManager {
   public:
    static DispatchTableManager& Get();

    // Instance dispatch tables
    VkuInstanceDispatchTable* InitInstanceTable(VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr);
    VkuInstanceDispatchTable* GetInstanceDispatchTable(const void* object) const;
    void DestroyInstanceTable(DispatchKey key);

    // Device dispatch tables
    VkuDeviceDispatchTable* InitDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr);
    VkuDeviceDispatchTable* GetDeviceDispatchTable(const void* object) const;
    void DestroyDeviceTable(DispatchKey key);

    // Loader data callbacks (from VK_LOADER_DATA_CALLBACK)
    void SetDeviceLoaderDataCallback(VkDevice device, PFN_vkSetDeviceLoaderData callback);
    PFN_vkSetDeviceLoaderData GetDeviceLoaderDataCallback(VkDevice device) const;

    // Reset all tables (useful for tests)
    void Clear();

   private:
    DispatchTableManager() = default;
    ~DispatchTableManager();
    DispatchTableManager(const DispatchTableManager&) = delete;
    DispatchTableManager& operator=(const DispatchTableManager&) = delete;

    mutable std::shared_mutex instance_mutex_;
    std::unordered_map<DispatchKey, std::unique_ptr<VkuInstanceDispatchTable>> instance_tables_;

    mutable std::shared_mutex device_mutex_;
    std::unordered_map<DispatchKey, std::unique_ptr<VkuDeviceDispatchTable>> device_tables_;

    mutable std::shared_mutex callback_mutex_;
    std::unordered_map<DispatchKey, PFN_vkSetDeviceLoaderData> loader_callbacks_;
};

/**
 * @brief Helper function to extract layer chain information from create info.
 */
VkLayerInstanceCreateInfo* GetChainInfo(const VkInstanceCreateInfo* create_info, VkLayerFunction function);
VkLayerDeviceCreateInfo* GetChainInfo(const VkDeviceCreateInfo* create_info, VkLayerFunction function);

template <typename MemberType>
struct MemberTraits;

template <typename Class, typename Member>
struct MemberTraits<Member Class::*> {
    using ClassType = Class;
};

/**
 * @brief Forwards a Vulkan API call downstream using the appropriate dispatch table.
 *
 * Automatically deduces whether MemberPtr belongs to VkuDeviceDispatchTable or
 * VkuInstanceDispatchTable, looks up the table using DispatchTableManager, checks
 * for null pointers, and invokes the function. If the table or function pointer
 * is null and the return type is VkResult, returns VK_SUCCESS.
 */
template <auto MemberPtr, typename Handle, typename... Args>
inline auto DispatchDownstream(Handle handle, Args&&... args) {
    using TableType = typename MemberTraits<decltype(MemberPtr)>::ClassType;
    TableType* table = nullptr;
    if constexpr (std::is_same_v<TableType, VkuInstanceDispatchTable>) {
        table = DispatchTableManager::Get().GetInstanceDispatchTable(handle);
    } else {
        table = DispatchTableManager::Get().GetDeviceDispatchTable(handle);
    }

    using FnPtr = decltype(table->*MemberPtr);
    using ReturnType = std::invoke_result_t<FnPtr, Handle, Args...>;

    if (table && table->*MemberPtr) {
        return (table->*MemberPtr)(handle, std::forward<Args>(args)...);
    }
    if constexpr (!std::is_void_v<ReturnType>) {
        if constexpr (std::is_same_v<ReturnType, VkResult>) {
            return VK_SUCCESS;
        } else {
            return ReturnType{};
        }
    }
}

}  // namespace layersvt
