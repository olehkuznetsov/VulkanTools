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

#include "layer_base.h"
#include "layer_keep_alive.h"
#include "device_instance_tracker.h"
#include "dispatch_table_manager.h"
#include "layer_manifest.h"

#include <cassert>

#if defined(_WIN32)
#include <crtdbg.h>
#include <windows.h>
#endif

namespace layersvt {

LayerBase::LayerBase(const LayerManifest* manifest) : manifest_(manifest) {}

VkResult LayerBase::CreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator,
                                   VkInstance* instance) {
#if defined(_WIN32) && defined(_CRTDBG_MODE_FILE)
#if !defined(NDEBUG)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    EnsureLayerKeepAlive();

    if (!create_info || !instance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstanceCreateInfo modified_create_info = *create_info;
    VkLayerInstanceCreateInfo* chain_info = GetChainInfo(&modified_create_info, VK_LAYER_LINK_INFO);
    if (!chain_info || !chain_info->u.pLayerInfo || !chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (create_instance == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PreCreateInstance(&modified_create_info, allocator);

    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = create_instance(&modified_create_info, allocator, instance);
    if (result == VK_SUCCESS && *instance != VK_NULL_HANDLE) {
        DispatchTableManager::Get().InitInstanceTable(*instance, get_instance_proc_addr);
        DeviceInstanceTracker::Get().EagerMapDevices(*instance, get_instance_proc_addr);
        PostCreateInstance(*instance, &modified_create_info);
    }
    return result;
}

void LayerBase::DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    if (instance == VK_NULL_HANDLE) {
        return;
    }

    PreDestroyInstance(instance, allocator);
    auto* table = DispatchTableManager::Get().GetInstanceDispatchTable(instance);
    DispatchKey key = GetDispatchKey(instance);
    if (table && table->DestroyInstance) {
        table->DestroyInstance(instance, allocator);
    }
    PostDestroyInstance(instance);
    DeviceInstanceTracker::Get().RemoveInstance(instance);
    DispatchTableManager::Get().DestroyInstanceTable(key);
}

VkResult LayerBase::EnumeratePhysicalDevices(VkInstance instance, uint32_t* physical_device_count,
                                             VkPhysicalDevice* physical_devices) {
    auto* table = DispatchTableManager::Get().GetInstanceDispatchTable(instance);
    if (!table || !table->EnumeratePhysicalDevices) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = table->EnumeratePhysicalDevices(instance, physical_device_count, physical_devices);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && physical_device_count && physical_devices) {
        for (uint32_t i = 0; i < *physical_device_count; ++i) {
            DeviceInstanceTracker::Get().SetVkInstance(physical_devices[i], instance);
        }
    }
    return result;
}

VkResult LayerBase::EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* physical_device_group_count,
                                                  VkPhysicalDeviceGroupProperties* physical_device_group_properties) {
    auto* table = DispatchTableManager::Get().GetInstanceDispatchTable(instance);
    if (!table || !table->EnumeratePhysicalDeviceGroups) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = table->EnumeratePhysicalDeviceGroups(instance, physical_device_group_count, physical_device_group_properties);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && physical_device_group_count && physical_device_group_properties) {
        for (uint32_t i = 0; i < *physical_device_group_count; ++i) {
            for (uint32_t j = 0; j < physical_device_group_properties[i].physicalDeviceCount; ++j) {
                DeviceInstanceTracker::Get().SetVkInstance(physical_device_group_properties[i].physicalDevices[j], instance);
            }
        }
    }
    return result;
}

VkResult LayerBase::CreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                 const VkAllocationCallbacks* allocator, VkDevice* device) {
    if (!create_info || !device || physical_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance instance = DeviceInstanceTracker::Get().GetVkInstance(physical_device);
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceCreateInfo modified_create_info = *create_info;
    VkLayerDeviceCreateInfo* chain_info = GetChainInfo(&modified_create_info, VK_LAYER_LINK_INFO);
    if (!chain_info || !chain_info->u.pLayerInfo || !chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr ||
        !chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;

    auto create_device = reinterpret_cast<PFN_vkCreateDevice>(get_instance_proc_addr(instance, "vkCreateDevice"));
    if (create_device == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Check for loader callback to initialize dispatchable handles created internally by the layer
    PFN_vkSetDeviceLoaderData loader_callback = nullptr;
    VkLayerDeviceCreateInfo* callback_info = GetChainInfo(&modified_create_info, VK_LOADER_DATA_CALLBACK);
    if (callback_info && callback_info->u.pfnSetDeviceLoaderData) {
        loader_callback = callback_info->u.pfnSetDeviceLoaderData;
    }

    PreCreateDevice(physical_device, &modified_create_info, allocator);

    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = create_device(physical_device, &modified_create_info, allocator, device);
    if (result == VK_SUCCESS && *device != VK_NULL_HANDLE) {
        DispatchTableManager::Get().InitDeviceTable(*device, get_device_proc_addr);
        if (loader_callback) {
            DispatchTableManager::Get().SetDeviceLoaderDataCallback(*device, loader_callback);
        }
        OnDeviceCreated(*device, physical_device, &modified_create_info);
    }
    return result;
}

void LayerBase::DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    PreDestroyDevice(device, allocator);
    auto* table = DispatchTableManager::Get().GetDeviceDispatchTable(device);
    DispatchKey key = GetDispatchKey(device);
    if (table && table->DestroyDevice) {
        table->DestroyDevice(device, allocator);
    }
    PostDestroyDevice(device);
    DispatchTableManager::Get().DestroyDeviceTable(key);
}

void LayerBase::PreCreateInstance(VkInstanceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PostCreateInstance(VkInstance, const VkInstanceCreateInfo*) {}
void LayerBase::PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}
void LayerBase::PostDestroyInstance(VkInstance) {}

void LayerBase::PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::OnDeviceCreated(VkDevice, VkPhysicalDevice, const VkDeviceCreateInfo*) {}
void LayerBase::PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) {}
void LayerBase::PostDestroyDevice(VkDevice) {}

}  // namespace layersvt
