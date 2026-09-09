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
#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace layersvt {

struct LayerManifest;
class LayerBaseTestPeer;

/**
 * Base class providing common infrastructure for Vulkan layer implementations.
 *
 * Implements the Template Method pattern for Vulkan API routing, centralizing loader
 * negotiation, dispatch table tracking, handle mapping, and property enumeration:
 *
 * - Singleton Lifecycle: A single LayerBase instance is created at library load time
 *   (typically as a file-scope static object in the layer's translation unit).
 * - Thread Safety: Internal registries (DispatchTableManager) are thread-safe.
 *   Overridden hooks called concurrently by Vulkan applications must maintain their
 *   own thread safety for layer-specific state.
 * - Command Routing: Custom commands return function pointers via GetLayerInstanceCommand
 *   and GetLayerDeviceCommand; unhandled commands route to downstream dispatch tables.
 * - Property Enumeration: Serves layer extensions and tool properties automatically
 *   from GetLayerManifest(), merging layer properties with downstream capabilities.
 * - Lifecycle Hooks: PreCreate* / PostCreate* / PreDestroy* hooks bracket instance and
 *   device creation and destruction. PreDestroy* hooks are guaranteed non-null handles
 *   (null handle calls return immediately per Vulkan Spec 2.7). No PostDestroy* hooks
 *   exist because downstream destruction frees and invalidates handles before returning.
 *
 * For authoring guides, CMake build setup, and downstream dispatch examples,
 * see layersvt/common/README.md.
 */
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

    /**
     * Retrieves the parent VkInstance associated with a physical device.
     * Returns the parent VkInstance on success, or VK_NULL_HANDLE if unregistered or invalid.
     */
    [[nodiscard]] static VkInstance GetVkInstance(VkPhysicalDevice physical_device);

    /**
     * Retrieves the loader data callback for initializing dispatchable handles created by layers.
     * Returns the registered PFN_vkSetDeviceLoaderData callback on success, or nullptr if unset.
     */
    [[nodiscard]] static PFN_vkSetDeviceLoaderData GetDeviceLoaderDataCallback(VkDevice device);

   protected:
    /**
     * Override to provide the layer's metadata, supported extensions, and tool properties.
     * Enables automatic handling of layer and extension property enumeration queries.
     */
    [[nodiscard]] virtual const LayerManifest* GetLayerManifest() const;

    // Extension hooks

    /**
     * Customizes or filters device extensions during vkEnumerateDeviceExtensionProperties.
     */
    virtual void ProcessDeviceExtensions(VkPhysicalDevice physical_device, const char* layer_name,
                                         std::vector<VkExtensionProperties>& extensions) const;

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

   private:
    [[nodiscard]] static VkuInstanceDispatchTable* GetInstanceDispatchTable(VkInstance instance);
    [[nodiscard]] static VkuInstanceDispatchTable* GetInstanceDispatchTable(VkPhysicalDevice physical_device);
    [[nodiscard]] static VkuDeviceDispatchTable* GetDeviceDispatchTable(const void* object);
    // Internal subsystems
    // Managed automatically by LayerBase; derived layers access downstream
    // dispatch via DispatchDownstream instead of querying these tables directly.
    DispatchTableManager dispatch_table_manager_;

    static inline LayerBase* layer_ = nullptr;

    // Exported Vulkan layer entry points (implemented in layer_entrypoints.cpp)
#if defined(_WIN32)
    friend VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ::vkGetInstanceProcAddr(VkInstance instance, const char* command_name);
    friend VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ::vkGetDeviceProcAddr(VkDevice device, const char* command_name);
    friend VKAPI_ATTR VkResult VKAPI_CALL ::vkEnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties);
    friend VKAPI_ATTR VkResult VKAPI_CALL ::vkEnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                                                  VkExtensionProperties* properties);
    friend VKAPI_ATTR VkResult VKAPI_CALL ::vkEnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                                              VkLayerProperties* properties);
    friend VKAPI_ATTR VkResult VKAPI_CALL ::vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                                                  uint32_t* property_count,
                                                                                  VkExtensionProperties* properties);
#else
    friend VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL (::vkGetInstanceProcAddr)(VkInstance instance, const char* command_name);
    friend VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL (::vkGetDeviceProcAddr)(VkDevice device, const char* command_name);
    friend VKAPI_ATTR VkResult VKAPI_CALL (::vkEnumerateInstanceLayerProperties)(uint32_t* property_count, VkLayerProperties* properties);
    friend VKAPI_ATTR VkResult VKAPI_CALL (::vkEnumerateInstanceExtensionProperties)(const char* layer_name, uint32_t* property_count,
                                                                                  VkExtensionProperties* properties);
    friend VKAPI_ATTR VkResult VKAPI_CALL (::vkEnumerateDeviceLayerProperties)(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                                              VkLayerProperties* properties);
    friend VKAPI_ATTR VkResult VKAPI_CALL (::vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice physical_device, const char* layer_name,
                                                                                  uint32_t* property_count,
                                                                                  VkExtensionProperties* properties);
#endif

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
    static VkResult VKAPI_CALL GetPhysicalDeviceToolProperties(VkPhysicalDevice physical_device, uint32_t* tool_count,
                                                               VkPhysicalDeviceToolPropertiesEXT* tool_properties);

    static PFN_vkVoidFunction GetKnownInstanceCommand(const char* command_name);
    static PFN_vkVoidFunction GetKnownDeviceCommand(const char* command_name);
};

}  // namespace layersvt
