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
#include "common/dispatch_table_manager.h"
#include "debug_marker.h"

// This file contains handwritten functions for the VK_EXT_debug_marker extension.
// We only actively implement vkDebugMarkerSetObjectNameEXT to track object names for Perfetto traces.
// All other functions are simple passthroughs required to be provided so that the layer
// can claim full support for the extension.

// We need to remap objects using getVkObjectType because VK_EXT_debug_marker uses the legacy
// VkDebugReportObjectTypeEXT enum, while we store everything using the modern VkObjectType
// enum to be consistent with VK_EXT_debug_utils and standard trace packets.
static inline VkObjectType getVkObjectType(VkDebugReportObjectTypeEXT vk_debug_report_object_type) {
    switch (vk_debug_report_object_type) {
        case VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT: return VK_OBJECT_TYPE_UNKNOWN;
        case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT: return VK_OBJECT_TYPE_INSTANCE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT: return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT: return VK_OBJECT_TYPE_DEVICE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT: return VK_OBJECT_TYPE_QUEUE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT: return VK_OBJECT_TYPE_SEMAPHORE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT: return VK_OBJECT_TYPE_COMMAND_BUFFER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT: return VK_OBJECT_TYPE_FENCE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT: return VK_OBJECT_TYPE_DEVICE_MEMORY;
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT: return VK_OBJECT_TYPE_BUFFER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT: return VK_OBJECT_TYPE_IMAGE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT: return VK_OBJECT_TYPE_EVENT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT: return VK_OBJECT_TYPE_QUERY_POOL;
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT: return VK_OBJECT_TYPE_BUFFER_VIEW;
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT: return VK_OBJECT_TYPE_IMAGE_VIEW;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT: return VK_OBJECT_TYPE_SHADER_MODULE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT: return VK_OBJECT_TYPE_PIPELINE_CACHE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT: return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT: return VK_OBJECT_TYPE_RENDER_PASS;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT: return VK_OBJECT_TYPE_PIPELINE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT: return VK_OBJECT_TYPE_SAMPLER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_SET;
        case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT: return VK_OBJECT_TYPE_FRAMEBUFFER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT: return VK_OBJECT_TYPE_COMMAND_POOL;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT: return VK_OBJECT_TYPE_SURFACE_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT: return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT: return VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_KHR_EXT: return VK_OBJECT_TYPE_DISPLAY_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_MODE_KHR_EXT: return VK_OBJECT_TYPE_DISPLAY_MODE_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_VALIDATION_CACHE_EXT_EXT: return VK_OBJECT_TYPE_VALIDATION_CACHE_EXT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_EXT: return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV_EXT: return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
        case VK_DEBUG_REPORT_OBJECT_TYPE_CU_MODULE_NVX_EXT: return VK_OBJECT_TYPE_CU_MODULE_NVX;
        case VK_DEBUG_REPORT_OBJECT_TYPE_CU_FUNCTION_NVX_EXT: return VK_OBJECT_TYPE_CU_FUNCTION_NVX;
        case VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT: return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA_EXT: return VK_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA;
        default:
            return VK_OBJECT_TYPE_UNKNOWN;
    }
}

extern "C" {

// Required for VK_EXT_debug_marker
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdDebugMarkerBeginEXT>(commandBuffer, pMarkerInfo);
}

// Required for VK_EXT_debug_marker
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdDebugMarkerEndEXT>(commandBuffer);
}

// Required for VK_EXT_debug_marker
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdDebugMarkerInsertEXT>(commandBuffer, pMarkerInfo);
}

// Required for VK_EXT_debug_marker. Tracks object name state.
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* pNameInfo) {
    if (pNameInfo) {
        DebugMarker::Get().SetDebugObjectName((uint64_t)device, (int32_t)getVkObjectType(pNameInfo->objectType), pNameInfo->object, pNameInfo->pObjectName);
    }
    return layersvt::DispatchDownstream<&VkuDeviceDispatchTable::DebugMarkerSetObjectNameEXT>(device, pNameInfo);
}

// Required for VK_EXT_debug_marker
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(VkDevice device, const VkDebugMarkerObjectTagInfoEXT* pTagInfo) {
    return layersvt::DispatchDownstream<&VkuDeviceDispatchTable::DebugMarkerSetObjectTagEXT>(device, pTagInfo);
}

} // extern "C"

