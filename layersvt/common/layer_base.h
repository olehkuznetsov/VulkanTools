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

#include "dispatch_table_manager.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace layersvt {

struct LayerManifest;
class LayerBaseTestPeer;

class LayerBase {
   public:
    LayerBase();
    virtual ~LayerBase();

    LayerBase(const LayerBase&) = delete;
    LayerBase& operator=(const LayerBase&) = delete;
    LayerBase(LayerBase&&) = delete;
    LayerBase& operator=(LayerBase&&) = delete;

    // Layer singleton management
    /**
     * Retrieves the currently active LayerBase singleton instance.
     * Returns a pointer to the active LayerBase instance, or nullptr if no layer is initialized.
     */
    [[nodiscard]] static LayerBase* Get() noexcept { return layer_; }

   protected:
    // Layer extension interface

    // Layer manifest

    /**
     * Override to provide the layer's metadata, supported extensions, and tool properties.
     * Enables automatic handling of layer and extension property enumeration queries.
     * Returns the layer's LayerManifest, or nullptr if none is configured.
     */
    [[nodiscard]] virtual const LayerManifest* GetLayerManifest() const { return nullptr; }

    // Extension and tooling hooks

    /**
     * Customizes or filters instance extensions during vkEnumerateInstanceExtensionProperties.
     */
    virtual void ProcessInstanceExtensions(const char* layer_name,
                                           std::vector<VkExtensionProperties>& extensions) const;

    /**
     * Customizes or filters device extensions during vkEnumerateDeviceExtensionProperties.
     */
    virtual void ProcessDeviceExtensions(VkPhysicalDevice physical_device, const char* layer_name,
                                         std::vector<VkExtensionProperties>& extensions) const;

    /**
     * Customizes or filters tool properties during vkGetPhysicalDeviceToolProperties.
     */
    virtual void ProcessToolProperties(VkPhysicalDevice physical_device,
                                       std::vector<VkPhysicalDeviceToolPropertiesEXT>& tools) const;

    /**
     * Indicates whether this layer intercepts physical device tool properties.
     * Returns true if tool properties are intercepted, or false otherwise.
     */
    [[nodiscard]] virtual bool HasToolProperties() const;

    // Layer-specific command intercepts

    /**
     * Override to intercept instance-level Vulkan commands.
     *
     * Returns a function pointer to the hook implementation, or nullptr to fall back
     * to core Vulkan intercepts (e.g. vkCreateInstance, vkDestroyInstance,
     * vkEnumeratePhysicalDevices) or downstream dispatch.
     */
    virtual PFN_vkVoidFunction GetLayerInstanceCommand(const char* command_name);

    /**
     * Override to intercept device-level Vulkan commands.
     *
     * Returns a function pointer to the hook implementation, or nullptr to fall back
     * to core Vulkan intercepts (e.g. vkDestroyDevice) or downstream dispatch.
     */
    virtual PFN_vkVoidFunction GetLayerDeviceCommand(const char* command_name);

    // Instance and device lifecycle hooks (template method pattern)

    /**
     * Hook called immediately before vkCreateInstance dispatches downstream.
     * Allows inspecting or modifying create_info (e.g. injecting extensions or pNext structs).
     */
    virtual void PreCreateInstance(VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator);

    /**
     * Hook called immediately after vkCreateInstance succeeds downstream.
     * Use to initialize instance state, settings, or tracing. The instance dispatch table is ready.
     */
    virtual void PostCreateInstance(VkInstance instance, const VkInstanceCreateInfo* create_info,
                                    const VkAllocationCallbacks* allocator);

    /**
     * Hook called immediately before vkDestroyInstance dispatches downstream.
     * Guaranteed to receive a valid, non-null VkInstance handle.
     */
    virtual void PreDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator);

    /**
     * Hook called immediately before vkCreateDevice dispatches downstream.
     * Allows inspecting or modifying create_info (e.g. injecting device extensions or pNext structs).
     */
    virtual void PreCreateDevice(VkPhysicalDevice physical_device, VkDeviceCreateInfo* create_info,
                                 const VkAllocationCallbacks* allocator);

    /**
     * Hook called immediately after vkCreateDevice succeeds downstream.
     * Use to initialize per-device state or allocate layer resources. The device dispatch table is ready.
     */
    virtual void PostCreateDevice(VkDevice device, VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                  const VkAllocationCallbacks* allocator);

    /**
     * Hook called immediately before vkDestroyDevice dispatches downstream.
     * Guaranteed to receive a valid, non-null VkDevice handle.
     */
    virtual void PreDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);

    /**
     * Retrieves the parent VkInstance associated with a physical device.
     * Returns the parent VkInstance on success, or VK_NULL_HANDLE if unregistered.
     */
    [[nodiscard]] static VkInstance GetVkInstance(VkPhysicalDevice physical_device);

    /**
     * Retrieves the loader data callback for initializing dispatchable handles created by layers.
     * Returns the registered PFN_vkSetDeviceLoaderData on success, or nullptr if unset.
     */
    [[nodiscard]] static PFN_vkSetDeviceLoaderData GetDeviceLoaderDataCallback(VkDevice device);

   private:
    [[nodiscard]] DispatchTableManager& GetDispatchTableManager() noexcept { return dispatch_table_manager_; }
    [[nodiscard]] const DispatchTableManager& GetDispatchTableManager() const noexcept { return dispatch_table_manager_; }

    [[nodiscard]] static VkuInstanceDispatchTable* GetInstanceDispatchTable(VkInstance instance);
    [[nodiscard]] static VkuInstanceDispatchTable* GetInstanceDispatchTable(VkPhysicalDevice physical_device);
    [[nodiscard]] static VkuDeviceDispatchTable* GetDeviceDispatchTable(const void* object);
    // Internal subsystems
    // Managed automatically by LayerBase; derived layers access downstream
    // dispatch via DispatchDownstream instead of querying these tables directly.
    DispatchTableManager dispatch_table_manager_;

    static inline LayerBase* layer_ = nullptr;

    friend class LayerBaseTestPeer;

    template <auto MemberPointer, typename Handle, typename... Args>
    friend auto DispatchDownstream(Handle handle, Args&&... args);

    template <auto MemberPointer, typename Fallback, typename Handle, typename... Args>
    friend auto DispatchDownstreamOr(Fallback&& fallback, Handle handle, Args&&... args);

    // Vulkan intercept commands (static C-compatible functions)

    static PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* command_name);
    static PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* command_name);

    static VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator,
                                              VkInstance* instance);
    static void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator);

    static VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance instance, uint32_t* physical_device_count,
                                                        VkPhysicalDevice* physical_devices);
    static VkResult VKAPI_CALL EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* physical_device_group_count,
                                                             VkPhysicalDeviceGroupProperties* physical_device_group_properties);
    static VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                            const VkAllocationCallbacks* allocator, VkDevice* device);
    static void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);

    static VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                                    VkExtensionProperties* properties);
    static VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties);
    static VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                              VkLayerProperties* properties);
    static VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                                  uint32_t* property_count, VkExtensionProperties* properties);
    static VkResult EnumerateDeviceExtensionPropertiesWithDownstream(
        VkPhysicalDevice physical_device, const char* layer_name, uint32_t* property_count,
        VkExtensionProperties* properties, PFN_vkEnumerateDeviceExtensionProperties downstream_function);
    static VkResult VKAPI_CALL GetPhysicalDeviceToolProperties(VkPhysicalDevice physical_device, uint32_t* tool_count,
                                                               VkPhysicalDeviceToolPropertiesEXT* tool_properties);
    static VkResult GetPhysicalDeviceToolPropertiesWithDownstream(
        VkPhysicalDevice physical_device, uint32_t* tool_count, VkPhysicalDeviceToolPropertiesEXT* tool_properties,
        PFN_vkGetPhysicalDeviceToolPropertiesEXT downstream_function);

    static PFN_vkVoidFunction GetKnownInstanceCommand(const char* command_name);
    static PFN_vkVoidFunction GetKnownDeviceCommand(const char* command_name);
};

}  // namespace layersvt
