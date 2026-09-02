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

extern "C" {

// This file contains handwritten functions for the VK_EXT_debug_utils extension.
// We only actively implement vkSetDebugUtilsObjectNameEXT to track object names for Perfetto traces.
// All other functions are simple passthroughs required to be provided so that the layer
// can claim full support for the extension.

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdBeginDebugUtilsLabelEXT>(commandBuffer, pLabelInfo);
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdEndDebugUtilsLabelEXT>(commandBuffer);
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdInsertDebugUtilsLabelEXT>(commandBuffer, pLabelInfo);
}

// Required for VK_EXT_debug_utils. Tracks object name state.
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo) {
    if (pNameInfo) {
        DebugMarker::Get().SetDebugObjectName((uint64_t)device, (int32_t)pNameInfo->objectType, pNameInfo->objectHandle, pNameInfo->pObjectName);
    }
    return layersvt::DispatchDownstream<&VkuDeviceDispatchTable::SetDebugUtilsObjectNameEXT>(device, pNameInfo);
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectTagEXT(VkDevice device, const VkDebugUtilsObjectTagInfoEXT* pTagInfo) {
    return layersvt::DispatchDownstream<&VkuDeviceDispatchTable::SetDebugUtilsObjectTagEXT>(device, pTagInfo);
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkQueueBeginDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::QueueBeginDebugUtilsLabelEXT>(queue, pLabelInfo);
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkQueueEndDebugUtilsLabelEXT(VkQueue queue) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::QueueEndDebugUtilsLabelEXT>(queue);
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkQueueInsertDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::QueueInsertDebugUtilsLabelEXT>(queue, pLabelInfo);
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
    return layersvt::DispatchDownstream<&VkuInstanceDispatchTable::CreateDebugUtilsMessengerEXT>(instance, pCreateInfo, pAllocator, pMessenger);
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator) {
    layersvt::DispatchDownstream<&VkuInstanceDispatchTable::DestroyDebugUtilsMessengerEXT>(instance, messenger, pAllocator);
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    layersvt::DispatchDownstream<&VkuInstanceDispatchTable::SubmitDebugUtilsMessageEXT>(instance, messageSeverity, messageTypes, pCallbackData);
}


} // extern "C"
