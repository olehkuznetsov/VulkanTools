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
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace layersvt {

/**
 * Thread-safe manager for Vulkan instance and device dispatch tables and physical device tracking.
 *
 * Manages dispatch tables keyed by handle dispatch key, maintains associations between
 * physical devices and parent instances, and tracks loader data callbacks
 * (VK_LOADER_DATA_CALLBACK) to initialize dispatchable objects created internally.
 */
class DispatchTableManager final {
   public:
    DispatchTableManager() = default;
    ~DispatchTableManager() = default;

    enum class Key : uintptr_t {};

    /**
     * Returns the dispatch key (first pointer-sized word) for a dispatchable Vulkan handle.
     */
    [[nodiscard]] static Key GetDispatchKey(const void* object) noexcept {
        assert(object != nullptr);
        return static_cast<Key>(reinterpret_cast<uintptr_t>(*reinterpret_cast<void* const*>(object)));
    }

    // Instance dispatch tables

    /**
     * Initializes and stores an instance dispatch table using downstream vkGetInstanceProcAddr.
     * Returns a non-null pointer to the stored dispatch table.
     */
    VkuInstanceDispatchTable* InitInstanceTable(VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr);

    /**
     * Looks up the instance dispatch table for a given instance handle.
     * Returns a pointer to the stored table on success, or nullptr if not registered.
     */
    [[nodiscard]] VkuInstanceDispatchTable* GetInstanceDispatchTable(VkInstance instance) const;

    /**
     * Looks up the instance dispatch table for a given physical device handle.
     * Resolves the parent instance and returns a pointer to its dispatch table on success, or nullptr if unregistered.
     */
    [[nodiscard]] VkuInstanceDispatchTable* GetInstanceDispatchTable(VkPhysicalDevice physical_device) const;

    /**
     * Overload for nullptr literal to resolve ambiguity between handle types. Always returns nullptr.
     */
    [[nodiscard]] VkuInstanceDispatchTable* GetInstanceDispatchTable(std::nullptr_t) const noexcept {
        return nullptr;
    }

    /**
     * Destroys the instance dispatch table and unmaps associated physical devices for the given dispatch key.
     * Callers should capture the Key beforehand via GetDispatchKey(...) before
     * downstream vkDestroyInstance invalidates the handle.
     */
    void DestroyInstanceTable(Key key);

    // Physical device tracking

    /**
     * Associates a physical device handle with its parent VkInstance.
     */
    void SetVkInstance(VkPhysicalDevice physical_device, VkInstance instance);

    /**
     * Associates multiple physical device handles with their parent VkInstance in a single atomic lock.
     */
    void RegisterPhysicalDevices(const VkPhysicalDevice* physical_devices, uint32_t count, VkInstance instance);

    /**
     * Retrieves the VkInstance associated with a physical device.
     * Returns the parent VkInstance on success, or VK_NULL_HANDLE if not registered.
     */
    [[nodiscard]] VkInstance GetVkInstance(VkPhysicalDevice physical_device) const;

    // Device dispatch tables

    /**
     * Initializes and stores a device dispatch table using downstream vkGetDeviceProcAddr.
     * Returns a non-null pointer to the stored dispatch table.
     */
    VkuDeviceDispatchTable* InitDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr);

    /**
     * Looks up the device dispatch table for a dispatchable object.
     * Returns a pointer to the stored table on success, or nullptr if object is null or unregistered.
     */
    [[nodiscard]] VkuDeviceDispatchTable* GetDeviceDispatchTable(const void* object) const;

    /**
     * Destroys the device dispatch table and loader callback for the given dispatch key.
     * Callers should capture the Key beforehand via GetDispatchKey(...) before
     * downstream vkDestroyDevice invalidates the handle.
     */
    void DestroyDeviceTable(Key key);

    // Loader data callbacks (from VK_LOADER_DATA_CALLBACK)

    /**
     * Registers the vkSetDeviceLoaderData callback for a logical device.
     */
    void SetDeviceLoaderDataCallback(VkDevice device, PFN_vkSetDeviceLoaderData callback);

    /**
     * Retrieves the vkSetDeviceLoaderData callback for a logical device.
     * Returns the registered callback on success, or nullptr if unset.
     */
    [[nodiscard]] PFN_vkSetDeviceLoaderData GetDeviceLoaderDataCallback(VkDevice device) const;

   private:
    DispatchTableManager(const DispatchTableManager&) = delete;
    DispatchTableManager& operator=(const DispatchTableManager&) = delete;
    DispatchTableManager(DispatchTableManager&&) = delete;
    DispatchTableManager& operator=(DispatchTableManager&&) = delete;

    mutable std::mutex instance_mutex_;
    std::unordered_map<Key, std::unique_ptr<VkuInstanceDispatchTable>> instance_tables_;
    std::unordered_map<Key, VkInstance> instance_keys_;
    std::unordered_map<VkPhysicalDevice, VkInstance> physical_device_to_instance_map_;

    mutable std::mutex device_mutex_;
    std::unordered_map<Key, std::unique_ptr<VkuDeviceDispatchTable>> device_tables_;
    std::unordered_map<Key, PFN_vkSetDeviceLoaderData> loader_callbacks_;
};

}  // namespace layersvt
