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
#include <mutex>

#include <map>
#include <string>

#include "common/layer_base.h"
#include "common/layer_manifest.h"

/**
 * The DebugMarker class is responsible for storing and managing debug marker
 * information associated with Vulkan objects and emitting them to Perfetto traces.
 *
 * Currently, it primarily supports tracking and emitting object names.
 *
 * How it works:
 * We do not store a history of events for setting object names. Instead, we store
 * only the current name for each object (one name per object). This keeps the memory
 * footprint small for most applications (proportional to the name size multiplied by
 * the number of unique objects).
 *
 * Perfetto Session Support:
 * This solution supports:
 * - Starting a Perfetto session before the application starts.
 * - Starting a Perfetto session after the application is already running.
 * - Running multiple Perfetto sessions during a single application run.
 *
 * To support late-attach and multiple sessions, when a Perfetto session starts,
 * we write all currently known object names to the trace. We retain the names in memory
 * because a user might start another Perfetto session later, requiring us to emit
 * all object names again.
 *
 * This class is a singleton, inherits from LayerBase, and provides thread-safe access to its state.
 */
class DebugMarker : public layersvt::LayerBase {
   public:
    /**
     * @brief Returns the singleton instance of the DebugMarker class.
     * @return Reference to the DebugMarker singleton.
     */
    static DebugMarker& Get();

    DebugMarker();
    ~DebugMarker() override = default;

    /**
     * @brief Lifecycle hook called before vkCreateInstance.
     */
    void PreCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator) override;

    /**
     * @brief Lifecycle hook called after vkDestroyDevice to remove tracked names for destroyed objects.
     */
    void PostDestroyDevice(VkDevice device) override;

    /**
     * @brief Sets or updates the name associated with a Vulkan object.
     * @param device The handle of the Vulkan device that owns the object.
     * @param type The type of the Vulkan object (represented as int32_t).
     * @param handle The handle of the Vulkan object.
     * @param name The name to associate with the object.
     */
    void SetDebugObjectName(uint64_t device, int32_t type, uint64_t handle, const char* name);
    
    /**
     * @brief Emits all stored debug markers to the tracing system.
     */
    void EmitAllDebugMarkers();

    /**
     * @brief Clears all stored debug markers and instance mappings.
     * @note This function is for testing only.
     */
    void Clear();
    
    /**
     * @brief Checks if a debug name is stored for a given object.
     * @note This function is for testing only.
     */
    bool HasDebugObjectName(int32_t type, uint64_t handle, const std::string& name);

    /**
     * @brief Associates a Vulkan physical device with its corresponding instance.
     * @param phys_dev The Vulkan physical device.
     * @param instance The Vulkan instance.
     */
    void SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance);

    /**
     * @brief Retrieves the Vulkan instance associated with a given physical device.
     * @param phys_dev The Vulkan physical device.
     * @return The associated Vulkan instance.
     */
    VkInstance GetVkInstance(VkPhysicalDevice phys_dev);

   protected:
    PFN_vkVoidFunction GetLayerSpecificInstanceFunction(const char* name) override;
    PFN_vkVoidFunction GetLayerSpecificDeviceFunction(const char* name) override;

   private:
    struct DebugObjectName {
        uint64_t vk_device;
        int32_t object_type;
        uint64_t handle;
        std::string name;

        DebugObjectName() = default;
        DebugObjectName(uint64_t dev, int32_t type, uint64_t h, const std::string& n)
            : vk_device(dev), object_type(type), handle(h), name(n) {}
    };

    std::mutex mutex_;
    /**
     * @brief Maps a pair of (object_type, object_handle) to its debug name information.
     * We use a pair as the key because handles are not guaranteed to be unique across different object types.
     */
    std::map<std::pair<int32_t, uint64_t>, DebugObjectName> debug_object_names_;
};

const layersvt::LayerManifest& GetDebugMarkerManifest();
