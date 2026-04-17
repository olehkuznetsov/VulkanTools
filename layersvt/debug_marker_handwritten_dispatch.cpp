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

#include "debug_marker_handwritten_functions.h"
#include "debug_marker_handwritten_functions_vk_ext_debug_marker.h"
#include "debug_marker_handwritten_functions_vk_ext_debug_utils.h"
#include "vk_layer_table.h"
#include <string.h>

extern "C" {

static PFN_vkVoidFunction debug_marker_known_instance_functions(const char* pName) {
    if (strcmp(pName, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (strcmp(pName, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDevices);
    if (strcmp(pName, "vkEnumeratePhysicalDeviceGroups") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDeviceGroups);
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if (strcmp(pName, "vkCreateDebugUtilsMessengerEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDebugUtilsMessengerEXT);
    if (strcmp(pName, "vkDestroyDebugUtilsMessengerEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDebugUtilsMessengerEXT);
    if (strcmp(pName, "vkSubmitDebugUtilsMessageEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSubmitDebugUtilsMessageEXT);
    return nullptr;
}

static PFN_vkVoidFunction debug_marker_known_device_functions(const char* pName) {
    if (strcmp(pName, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    
    // VK_EXT_debug_marker
    if (strcmp(pName, "vkCmdDebugMarkerBeginEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerBeginEXT);
    if (strcmp(pName, "vkCmdDebugMarkerEndEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerEndEXT);
    if (strcmp(pName, "vkCmdDebugMarkerInsertEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerInsertEXT);
    if (strcmp(pName, "vkDebugMarkerSetObjectNameEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectNameEXT);
    if (strcmp(pName, "vkDebugMarkerSetObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectTagEXT);

    // VK_EXT_debug_utils
    if (strcmp(pName, "vkCmdBeginDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginDebugUtilsLabelEXT);
    if (strcmp(pName, "vkCmdEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndDebugUtilsLabelEXT);
    if (strcmp(pName, "vkCmdInsertDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdInsertDebugUtilsLabelEXT);
    if (strcmp(pName, "vkSetDebugUtilsObjectNameEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT);
    if (strcmp(pName, "vkSetDebugUtilsObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectTagEXT);
    if (strcmp(pName, "vkQueueBeginDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueBeginDebugUtilsLabelEXT);
    if (strcmp(pName, "vkQueueEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueEndDebugUtilsLabelEXT);
    if (strcmp(pName, "vkQueueInsertDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueInsertDebugUtilsLabelEXT);

    return nullptr;
}

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    PFN_vkVoidFunction func = debug_marker_known_instance_functions(pName);
    if (func) {
        return func;
    }
    
    // If it's a device function, we can also return it here if we want to support GIPA for device functions.
    func = debug_marker_known_device_functions(pName);
    if (func) {
        return func;
    }

    if (instance == nullptr) {
        return nullptr;
    }

    auto table = instance_dispatch_table(instance);
    if (table == NULL) {
        return nullptr;
    }
    
    if (table->GetInstanceProcAddr == NULL) {
        return nullptr;
    }

    return table->GetInstanceProcAddr(instance, pName);
}

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    PFN_vkVoidFunction func = debug_marker_known_device_functions(pName);
    if (func) {
        return func;
    }

    if (device == nullptr) {
        return nullptr;
    }

    if (device_dispatch_table(device)->GetDeviceProcAddr == NULL) {
        return nullptr;
    }
    
    return device_dispatch_table(device)->GetDeviceProcAddr(device, pName);
}

} // extern "C"
