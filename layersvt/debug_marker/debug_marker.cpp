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
#include "perfetto/perfetto.h"
#include <cstring>

namespace {

// The packet shares the track event sequence, whose defaults declare an incremental clock, so the
// absolute timestamp must carry an explicit clock id to not be read as a delta.
// An empty object_name, emitted by RemoveDebugObjectName, means the object is unnamed from this
// packet's timestamp on.
void EmitDebugObjectNamePacket(uint64_t device, int32_t type, uint64_t handle, const std::string& name) {
    perfetto::TrackEvent::Trace([device, type, handle, &name](perfetto::TrackEvent::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(perfetto::TrackEvent::GetTraceTimeNs());
        packet->set_timestamp_clock_id(static_cast<uint32_t>(perfetto::TrackEvent::GetTraceClockId()));
        auto event = packet->set_vulkan_api_event()->set_vk_debug_utils_object_name();
        event->set_pid(static_cast<uint32_t>(perfetto::base::GetProcessId()));
        event->set_vk_device(device);
        event->set_object_type(type);
        event->set_object(handle);
        event->set_object_name(name.data(), name.size());
    });
}

// Names are removed before the downstream call: once destruction returns, the driver may hand out
// the same handle value to a new object created on another thread.
template <auto DestroyCommand, VkObjectType ObjectType, typename Handle>
VKAPI_ATTR void VKAPI_CALL DestroyNamedObject(VkDevice device, Handle handle, const VkAllocationCallbacks* pAllocator) {
    DebugMarker::Get().RemoveDebugObjectName((uint64_t)device, ObjectType, (uint64_t)handle);
    layersvt::DispatchDownstream<DestroyCommand>(device, handle, pAllocator);
}

template <auto DestroyCommand, VkObjectType ObjectType, typename Handle>
PFN_vkVoidFunction DestroyHookFunction() {
    return reinterpret_cast<PFN_vkVoidFunction>(&DestroyNamedObject<DestroyCommand, ObjectType, Handle>);
}

struct DestroyHook {
    const char* command_name;
    PFN_vkVoidFunction function;
    bool is_extension = false;
};

// Covers object types whose live handles are unique in practice. Samplers, sampler YCbCr conversions,
// pipeline layouts and descriptor set layouts are excluded: drivers may return the same handle for
// identical create infos, so destroying one copy must not remove the name of another live copy. The spec
// guarantees unique non-dispatchable handles only when the privateData feature is enabled, so this split
// is a practical heuristic, not a hard rule.
//
// Command buffers and descriptor sets are not covered. vkFreeCommandBuffers and vkFreeDescriptorSets free
// them one at a time, but take an array of handles, which DestroyNamedObject does not support.
// vkDestroyCommandPool, vkResetDescriptorPool and vkDestroyDescriptorPool free them implicitly, which needs
// pool-to-child tracking. Their names stay until the device is destroyed.
//
// The list is deliberately partial. VkShaderEXT, VkDescriptorUpdateTemplate, VkVideoSessionKHR,
// VkVideoSessionParametersKHR and VkMicromapEXT, among others, are not covered yet. Lookup is by command
// name, so a promoted command also needs an entry for each alias (for example
// vkDestroyDescriptorUpdateTemplateKHR).
const DestroyHook kDestroyHooks[] = {
    {"vkFreeMemory", DestroyHookFunction<&VkuDeviceDispatchTable::FreeMemory, VK_OBJECT_TYPE_DEVICE_MEMORY, VkDeviceMemory>()},
    {"vkDestroyBuffer", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyBuffer, VK_OBJECT_TYPE_BUFFER, VkBuffer>()},
    {"vkDestroyImage", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyImage, VK_OBJECT_TYPE_IMAGE, VkImage>()},
    {"vkDestroyImageView", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyImageView, VK_OBJECT_TYPE_IMAGE_VIEW, VkImageView>()},
    {"vkDestroyBufferView",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyBufferView, VK_OBJECT_TYPE_BUFFER_VIEW, VkBufferView>()},
    {"vkDestroyFramebuffer",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyFramebuffer, VK_OBJECT_TYPE_FRAMEBUFFER, VkFramebuffer>()},
    {"vkDestroyRenderPass",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyRenderPass, VK_OBJECT_TYPE_RENDER_PASS, VkRenderPass>()},
    {"vkDestroyPipeline", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyPipeline, VK_OBJECT_TYPE_PIPELINE, VkPipeline>()},
    {"vkDestroyShaderModule",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyShaderModule, VK_OBJECT_TYPE_SHADER_MODULE, VkShaderModule>()},
    {"vkDestroyPipelineCache",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyPipelineCache, VK_OBJECT_TYPE_PIPELINE_CACHE, VkPipelineCache>()},
    {"vkDestroyQueryPool", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyQueryPool, VK_OBJECT_TYPE_QUERY_POOL, VkQueryPool>()},
    {"vkDestroyFence", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyFence, VK_OBJECT_TYPE_FENCE, VkFence>()},
    {"vkDestroySemaphore", DestroyHookFunction<&VkuDeviceDispatchTable::DestroySemaphore, VK_OBJECT_TYPE_SEMAPHORE, VkSemaphore>()},
    {"vkDestroyEvent", DestroyHookFunction<&VkuDeviceDispatchTable::DestroyEvent, VK_OBJECT_TYPE_EVENT, VkEvent>()},
    {"vkDestroyCommandPool",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyCommandPool, VK_OBJECT_TYPE_COMMAND_POOL, VkCommandPool>()},
    {"vkDestroyDescriptorPool",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyDescriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, VkDescriptorPool>()},
    {"vkDestroySwapchainKHR",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroySwapchainKHR, VK_OBJECT_TYPE_SWAPCHAIN_KHR, VkSwapchainKHR>(), true},
    {"vkDestroyAccelerationStructureKHR",
     DestroyHookFunction<&VkuDeviceDispatchTable::DestroyAccelerationStructureKHR, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
                         VkAccelerationStructureKHR>(),
     true},
};

}  // namespace

DebugMarker::DebugMarker() = default;

const layersvt::LayerManifest* DebugMarker::GetLayerManifest() const {
    static const layersvt::LayerManifest manifest{
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
    };
    return &manifest;
}

void DebugMarker::PreCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator) {
    (void)pCreateInfo;
    (void)pAllocator;
    static std::once_flag perfetto_initialization_flag;
    std::call_once(perfetto_initialization_flag, []() { InitializeDebugMarkerPerfetto(); });
}

void DebugMarker::PreDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    (void)pAllocator;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = emulated_messengers_.begin(); it != emulated_messengers_.end();) {
        if (it->second.instance == instance) {
            it = emulated_messengers_.erase(it);
        } else {
            ++it;
        }
    }
}

void DebugMarker::PreDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    (void)pAllocator;
    uint64_t dev_handle = (uint64_t)device;
    std::vector<DebugObjectKey> removed_objects;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = debug_object_names_.begin(); it != debug_object_names_.end();) {
            if (it->first.vk_device == dev_handle) {
                removed_objects.push_back(it->first);
                it = debug_object_names_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Tell trace consumers that the objects of this device are unnamed from here on. The packets are
    // emitted after mutex_ is released, because the session start observer takes mutex_ from the tracing
    // thread (EmitAllDebugMarkers). The device is still alive here, so its handles cannot be reused yet.
    for (const auto& removed : removed_objects) {
        EmitDebugObjectNamePacket(removed.vk_device, removed.object_type, removed.handle, std::string());
    }
}

void DebugMarker::SetDebugObjectName(uint64_t device, int32_t type, uint64_t handle, const char* name) {
    if (name == nullptr || name[0] == '\0') {
        RemoveDebugObjectName(device, type, handle);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::string name_str = name;
    debug_object_names_[{device, type, handle}] = name_str;
    EmitDebugObjectNamePacket(device, type, handle, name_str);
}

void DebugMarker::RemoveDebugObjectName(uint64_t device, int32_t type, uint64_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (debug_object_names_.erase({device, type, handle}) == 0) return;
    // An empty name tells trace consumers the object is unnamed from here on.
    EmitDebugObjectNamePacket(device, type, handle, std::string());
}

void DebugMarker::EmitAllDebugMarkers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : debug_object_names_) {
        const DebugObjectKey& key = entry.first;
        EmitDebugObjectNamePacket(key.vk_device, key.object_type, key.handle, entry.second);
    }
}

std::optional<std::string> DebugMarker::GetDebugObjectName(uint64_t device, int32_t type, uint64_t handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = debug_object_names_.find({device, type, handle});
    if (it == debug_object_names_.end()) return std::nullopt;
    return it->second;
}

VkResult DebugMarker::CreateEmulatedMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                              VkDebugUtilsMessengerEXT* pMessenger) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t handle = next_emulated_messenger_++;
    EmulatedMessenger& messenger = emulated_messengers_[handle];
    messenger.instance = instance;
    messenger.create_info = *pCreateInfo;
    // The pNext chain is owned by the application and is not used by the emulation.
    messenger.create_info.pNext = nullptr;
    *pMessenger = (VkDebugUtilsMessengerEXT)handle;
    return VK_SUCCESS;
}

void DebugMarker::DestroyEmulatedMessenger(VkDebugUtilsMessengerEXT messenger) {
    std::lock_guard<std::mutex> lock(mutex_);
    emulated_messengers_.erase((uint64_t)messenger);
}

void DebugMarker::SubmitEmulatedMessage(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    std::vector<VkDebugUtilsMessengerCreateInfoEXT> matching;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : emulated_messengers_) {
            const VkDebugUtilsMessengerCreateInfoEXT& info = entry.second.create_info;
            if (entry.second.instance == instance && (info.messageSeverity & messageSeverity) != 0 &&
                (info.messageType & messageTypes) != 0) {
                matching.push_back(info);
            }
        }
    }
    // Invoke callbacks without holding the lock so that they may call back into the layer.
    for (const auto& info : matching) {
        info.pfnUserCallback(messageSeverity, messageTypes, pCallbackData, info.pUserData);
    }
}

PFN_vkVoidFunction DebugMarker::GetLayerInstanceCommand(VkInstance instance, const char* name) {
    (void)instance;
    if (strcmp(name, "vkCreateDebugUtilsMessengerEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDebugUtilsMessengerEXT);
    if (strcmp(name, "vkDestroyDebugUtilsMessengerEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDebugUtilsMessengerEXT);
    if (strcmp(name, "vkSubmitDebugUtilsMessageEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkSubmitDebugUtilsMessageEXT);
    return nullptr;
}

PFN_vkVoidFunction DebugMarker::GetLayerDeviceCommand(VkDevice device, const char* name) {

    // VK_EXT_debug_marker
    if (strcmp(name, "vkCmdDebugMarkerBeginEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerBeginEXT);
    if (strcmp(name, "vkCmdDebugMarkerEndEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerEndEXT);
    if (strcmp(name, "vkCmdDebugMarkerInsertEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerInsertEXT);
    if (strcmp(name, "vkDebugMarkerSetObjectNameEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectNameEXT);
    if (strcmp(name, "vkDebugMarkerSetObjectTagEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectTagEXT);

    // VK_EXT_debug_utils
    if (strcmp(name, "vkCmdBeginDebugUtilsLabelEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginDebugUtilsLabelEXT);
    if (strcmp(name, "vkCmdEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndDebugUtilsLabelEXT);
    if (strcmp(name, "vkCmdInsertDebugUtilsLabelEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdInsertDebugUtilsLabelEXT);
    if (strcmp(name, "vkSetDebugUtilsObjectNameEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT);
    if (strcmp(name, "vkSetDebugUtilsObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectTagEXT);
    if (strcmp(name, "vkQueueBeginDebugUtilsLabelEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueBeginDebugUtilsLabelEXT);
    if (strcmp(name, "vkQueueEndDebugUtilsLabelEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueEndDebugUtilsLabelEXT);
    if (strcmp(name, "vkQueueInsertDebugUtilsLabelEXT") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueInsertDebugUtilsLabelEXT);

    for (const DestroyHook& hook : kDestroyHooks) {
        if (strcmp(name, hook.command_name) != 0) continue;
        // Extension commands are only intercepted when the downstream device provides them.
        if (hook.is_extension && device != VK_NULL_HANDLE &&
            layersvt::DispatchDownstreamOr<&VkuDeviceDispatchTable::GetDeviceProcAddr>(nullptr, device, name) == nullptr) {
            return nullptr;
        }
        return hook.function;
    }

    return nullptr;
}
