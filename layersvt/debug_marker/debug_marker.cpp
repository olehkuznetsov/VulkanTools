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

#include "debug_marker.h"
#include "debug_marker_perfetto.h"
#include "debug_marker_handwritten_functions_vk_ext_debug_marker.h"
#include "debug_marker_handwritten_functions_vk_ext_debug_utils.h"
#include "common/device_instance_tracker.h"
#include "perfetto/perfetto.h"
#include <cstring>

const layersvt::LayerManifest& GetDebugMarkerManifest() {
    static const layersvt::LayerManifest manifest(layersvt::LayerManifest::Config{
        .layer_name = "VK_LAYER_GOOGLE_DebugMarker",
        .description = "layer: DebugMarker",
        .spec_version = VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION),
        .implementation_version = VK_MAKE_VERSION(0, 1, 0),
        .instance_extensions =
            {
                {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION},
            },
        .device_extensions =
            {
                {VK_EXT_DEBUG_MARKER_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_SPEC_VERSION},
            },
        .tool_properties = std::nullopt,
    });
    return manifest;
}

DebugMarker::DebugMarker() : LayerBase(&GetDebugMarkerManifest()) {}

DebugMarker& DebugMarker::Get() {
    static DebugMarker instance;
    return instance;
}

void DebugMarker::PreCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator) {
    (void)pCreateInfo;
    (void)pAllocator;
    static std::once_flag perfetto_initialization_flag;
    std::call_once(perfetto_initialization_flag, []() { InitializeDebugMarkerPerfetto(); });
}

void DebugMarker::PostDestroyDevice(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t dev_handle = (uint64_t)device;
    for (auto it = debug_object_names_.begin(); it != debug_object_names_.end();) {
        if (it->second.vk_device == dev_handle) {
            it = debug_object_names_.erase(it);
        } else {
            ++it;
        }
    }
}

void DebugMarker::SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance) {
    layersvt::DeviceInstanceTracker::Get().SetVkInstance(phys_dev, instance);
}

VkInstance DebugMarker::GetVkInstance(VkPhysicalDevice phys_dev) {
    return layersvt::DeviceInstanceTracker::Get().GetVkInstance(phys_dev);
}

void DebugMarker::SetDebugObjectName(uint64_t device, int32_t type, uint64_t handle, const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string name_str = name ? name : "NULL";
    debug_object_names_[std::make_pair(type, handle)] = DebugObjectName(device, type, handle, name_str);

    perfetto::TrackEvent::Trace([device, type, handle, name_str](perfetto::TrackEvent::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(perfetto::base::GetBootTimeNs().count());
        auto event = packet->set_vulkan_api_event()->set_vk_debug_utils_object_name();
        event->set_vk_device(device);
        event->set_object_type(type);
        event->set_object(handle);
        event->set_object_name(name_str.c_str());
    });
}

void DebugMarker::EmitAllDebugMarkers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : debug_object_names_) {
        const auto& marker = entry.second;
        uint64_t device = marker.vk_device;
        int32_t type = marker.object_type;
        uint64_t handle = marker.handle;
        std::string name_str = marker.name;
        
        perfetto::TrackEvent::Trace([device, type, handle, name_str](perfetto::TrackEvent::TraceContext ctx) {
            auto packet = ctx.NewTracePacket();
            packet->set_timestamp(perfetto::base::GetBootTimeNs().count());
            auto event = packet->set_vulkan_api_event()->set_vk_debug_utils_object_name();
            event->set_vk_device(device);
            event->set_object_type(type);
            event->set_object(handle);
            event->set_object_name(name_str.c_str());
        });
    }
}

void DebugMarker::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    debug_object_names_.clear();
    layersvt::DeviceInstanceTracker::Get().Clear();
}

bool DebugMarker::HasDebugObjectName(int32_t type, uint64_t handle, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = debug_object_names_.find(std::make_pair(type, handle));
    if (it == debug_object_names_.end()) return false;
    return it->second.name == name;
}

PFN_vkVoidFunction DebugMarker::GetLayerSpecificInstanceFunction(const char* name) {
    if (!name) return nullptr;
    if (strcmp(name, "vkCreateDebugUtilsMessengerEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDebugUtilsMessengerEXT);
    if (strcmp(name, "vkDestroyDebugUtilsMessengerEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDebugUtilsMessengerEXT);
    if (strcmp(name, "vkSubmitDebugUtilsMessageEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSubmitDebugUtilsMessageEXT);
    return nullptr;
}

PFN_vkVoidFunction DebugMarker::GetLayerSpecificDeviceFunction(const char* name) {
    if (!name) return nullptr;

    // VK_EXT_debug_marker
    if (strcmp(name, "vkCmdDebugMarkerBeginEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerBeginEXT);
    if (strcmp(name, "vkCmdDebugMarkerEndEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerEndEXT);
    if (strcmp(name, "vkCmdDebugMarkerInsertEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerInsertEXT);
    if (strcmp(name, "vkDebugMarkerSetObjectNameEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectNameEXT);
    if (strcmp(name, "vkDebugMarkerSetObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectTagEXT);

    // VK_EXT_debug_utils
    if (strcmp(name, "vkCmdBeginDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginDebugUtilsLabelEXT);
    if (strcmp(name, "vkCmdEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndDebugUtilsLabelEXT);
    if (strcmp(name, "vkCmdInsertDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdInsertDebugUtilsLabelEXT);
    if (strcmp(name, "vkSetDebugUtilsObjectNameEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT);
    if (strcmp(name, "vkSetDebugUtilsObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectTagEXT);
    if (strcmp(name, "vkQueueBeginDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueBeginDebugUtilsLabelEXT);
    if (strcmp(name, "vkQueueEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueEndDebugUtilsLabelEXT);
    if (strcmp(name, "vkQueueInsertDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueInsertDebugUtilsLabelEXT);

    return nullptr;
}
