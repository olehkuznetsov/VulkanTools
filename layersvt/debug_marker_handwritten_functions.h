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
#include <vector>
#include <assert.h>
#include <string.h>
#include "vk_layer_table.h"
#include "debug_marker.h"
#include "debug_marker_perfetto.h"

// This file contains handwritten implementations for core Vulkan functions
// (instance/device creation and physical device enumeration) required for the layer's
// infrastructure and state management:
//
// - vkCreateInstance: Initializes Perfetto tracing, the instance dispatch table, and performs eager physical device enumeration.
// - vkEnumeratePhysicalDevices / vkEnumeratePhysicalDeviceGroups: Tracks the mapping
//   between physical devices and instances to support dispatch table lookups.
// - vkCreateDevice: Initializes the device dispatch table for intercepted devices.
//
// Extension-specific functions (e.g., VK_EXT_debug_marker, VK_EXT_debug_utils)
// are located in separate dedicated header files.

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#if defined(__GNUC__) && __GNUC__ >= 4
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#else
#define EXPORT_FUNCTION
#endif

static std::once_flag g_perfetto_init_flag;


extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                                VkInstance* pInstance) {
    std::call_once(g_perfetto_init_flag, []() { InitializeDebugMarkerPerfetto(); });

    // Get the function pointer
    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    assert(fpGetInstanceProcAddr != 0);
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS) {
        initInstanceTable(*pInstance, fpGetInstanceProcAddr);
        
        // Eagerly enumerate physical devices and map them to the instance.
        // This ensures we have the mapping even if the app bypasses our enumeration hooks.
        PFN_vkEnumeratePhysicalDevices fpEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)fpGetInstanceProcAddr(*pInstance, "vkEnumeratePhysicalDevices");
        if (fpEnumeratePhysicalDevices) {
            uint32_t count = 0;
            fpEnumeratePhysicalDevices(*pInstance, &count, nullptr);
            if (count > 0) {
                std::vector<VkPhysicalDevice> devices(count);
                fpEnumeratePhysicalDevices(*pInstance, &count, devices.data());
                for (uint32_t i = 0; i < count; ++i) {
                    DebugMarker::Get().SetVkInstance(devices[i], *pInstance);
                }
            }
        }
    }

    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    if (instance_dispatch_table(instance)->EnumeratePhysicalDevices == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VkResult result = instance_dispatch_table(instance)->EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDevices != nullptr) {
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            DebugMarker::Get().SetVkInstance(pPhysicalDevices[i], instance);
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    if (instance_dispatch_table(instance)->EnumeratePhysicalDeviceGroups == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VkResult result = instance_dispatch_table(instance)->EnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
    
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDeviceGroupProperties != nullptr) {
        for (uint32_t i = 0; i < *pPhysicalDeviceGroupCount; ++i) {
            for (uint32_t j = 0; j < pPhysicalDeviceGroupProperties[i].physicalDeviceCount; ++j) {
                DebugMarker::Get().SetVkInstance(pPhysicalDeviceGroupProperties[i].physicalDevices[j], instance);
            }
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    dispatch_key key = get_dispatch_key(instance);
    instance_dispatch_table(instance)->DestroyInstance(instance, pAllocator);
    destroy_instance_dispatch_table(key);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    // Get the function pointer
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    VkInstance vk_instance = DebugMarker::Get().GetVkInstance(physicalDevice);
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(vk_instance, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS) {
        initDeviceTable(*pDevice, fpGetDeviceProcAddr);
    }

    return result;
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                                      uint32_t* pPropertyCount,
                                                                                      VkExtensionProperties* pProperties) {
    static const VkExtensionProperties instanceExtensions[] = {
        {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION},
    };

    if (pLayerName != nullptr && strcmp(pLayerName, "VK_LAYER_GOOGLE_DebugMarker") == 0) {
        return util_GetExtensionProperties(ARRAY_SIZE(instanceExtensions), instanceExtensions, pPropertyCount, pProperties);
    }

    return util_GetExtensionProperties(0, nullptr, pPropertyCount, pProperties);
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                                                  VkLayerProperties* pProperties) {
    static const VkLayerProperties layerProperties[] = {{
        "VK_LAYER_GOOGLE_DebugMarker",
        VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION),  // specVersion
        VK_MAKE_VERSION(0, 1, 0),                  // implementationVersion
        "layer: DebugMarker",
    }};

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                                                                uint32_t* pPropertyCount,
                                                                                VkLayerProperties* pProperties) {
    static const VkLayerProperties layerProperties[] = {{
        "VK_LAYER_GOOGLE_DebugMarker",
        VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION),
        VK_MAKE_VERSION(0, 1, 0),
        "layer: DebugMarker",
    }};

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                    const char* pLayerName,
                                                                                    uint32_t* pPropertyCount,
                                                                                    VkExtensionProperties* pProperties) {
    static const VkExtensionProperties deviceExtensions[] = {
        {VK_EXT_DEBUG_MARKER_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_SPEC_VERSION},
        {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION},
    };

    if (pLayerName != nullptr && strcmp(pLayerName, "VK_LAYER_GOOGLE_DebugMarker") == 0) {
        return util_GetExtensionProperties(ARRAY_SIZE(deviceExtensions), deviceExtensions, pPropertyCount, pProperties);
    }

    VkInstance vk_instance = DebugMarker::Get().GetVkInstance(physicalDevice);

    // Manually append device extension.  This should not be necessary, but the Android vulkan
    // loader does not expose extensions from implicit layer (b/143293104).
    if (pProperties == nullptr) {
        VkResult res = instance_dispatch_table(vk_instance)->EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
        if (res == VK_SUCCESS) {
            (*pPropertyCount) += ARRAY_SIZE(deviceExtensions);
        }
        return res;
    }
    
    if (*pPropertyCount > 0) {
        uint32_t requestedCount = *pPropertyCount;
        VkResult res = instance_dispatch_table(vk_instance)->EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
        if (res == VK_SUCCESS) {
            uint32_t originalCount = *pPropertyCount;
            uint32_t additionalCount = 0;
            
            for (uint32_t i = 0; i < ARRAY_SIZE(deviceExtensions); ++i) {
                bool found = false;
                for (uint32_t j = 0; j < originalCount; ++j) {
                    if (strcmp(pProperties[j].extensionName, deviceExtensions[i].extensionName) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (originalCount + additionalCount < requestedCount) {
                        pProperties[originalCount + additionalCount] = deviceExtensions[i];
                    }
                    additionalCount++;
                }
            }
            *pPropertyCount = originalCount + additionalCount;
            if (*pPropertyCount > requestedCount) {
                *pPropertyCount = requestedCount;
            }
        }
        return res;
    }
    return VK_SUCCESS;
}


} // extern "C"
