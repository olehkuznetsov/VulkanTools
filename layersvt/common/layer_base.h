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
#include <cstdint>

namespace layersvt {

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
    // Layer-specific command intercepts

    /**
     * Override to intercept instance-level Vulkan commands.
     *
     * Returns a function pointer to the hook implementation, or nullptr to fall back
     * to core Vulkan intercepts and downstream dispatch.
     */
    virtual PFN_vkVoidFunction GetLayerInstanceCommand(const char* command_name);

    /**
     * Override to intercept device-level Vulkan commands.
     *
     * Returns a function pointer to the hook implementation, or nullptr to fall back
     * to core Vulkan intercepts and downstream dispatch.
     */
    virtual PFN_vkVoidFunction GetLayerDeviceCommand(const char* command_name);

    static VkInstance GetVkInstance(VkPhysicalDevice physical_device);

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

    static PFN_vkVoidFunction GetKnownInstanceCommand(const char* command_name);
    static PFN_vkVoidFunction GetKnownDeviceCommand(const char* command_name);
};

}  // namespace layersvt
