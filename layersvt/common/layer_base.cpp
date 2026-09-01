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
#include <cstring>

#if defined(_WIN32)
#include <crtdbg.h>
#include <windows.h>
#endif

extern "C" {

static VkResult VKAPI_CALL LayerBaseCreateInstance(const VkInstanceCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocator,
                                                   VkInstance* instance) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->CreateInstance(create_info, allocator, instance);
}

static void VKAPI_CALL LayerBaseDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    if (layersvt::LayerBase::GetActiveLayer()) {
        layersvt::LayerBase::GetActiveLayer()->DestroyInstance(instance, allocator);
    }
}

static VkResult VKAPI_CALL LayerBaseEnumeratePhysicalDevices(VkInstance instance, uint32_t* physical_device_count,
                                                            VkPhysicalDevice* physical_devices) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->EnumeratePhysicalDevices(instance, physical_device_count, physical_devices);
}

static VkResult VKAPI_CALL LayerBaseEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* physical_device_group_count,
                                                               VkPhysicalDeviceGroupProperties* physical_device_group_properties) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->EnumeratePhysicalDeviceGroups(instance, physical_device_group_count, physical_device_group_properties);
}

static VkResult VKAPI_CALL LayerBaseCreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                                const VkAllocationCallbacks* allocator, VkDevice* device) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->CreateDevice(physical_device, create_info, allocator, device);
}

static void VKAPI_CALL LayerBaseDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    if (layersvt::LayerBase::GetActiveLayer()) {
        layersvt::LayerBase::GetActiveLayer()->DestroyDevice(device, allocator);
    }
}

static VkResult VKAPI_CALL LayerBaseEnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                                        VkExtensionProperties* properties) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->EnumerateInstanceExtensionProperties(layer_name, property_count, properties);
}

static VkResult VKAPI_CALL LayerBaseEnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->EnumerateInstanceLayerProperties(property_count, properties);
}

static VkResult VKAPI_CALL LayerBaseEnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                                      uint32_t* property_count, VkExtensionProperties* properties) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->EnumerateDeviceExtensionProperties(physical_device, layer_name, property_count, properties);
}

static VkResult VKAPI_CALL LayerBaseEnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                                  VkLayerProperties* properties) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return layersvt::LayerBase::GetActiveLayer()->EnumerateDeviceLayerProperties(physical_device, property_count, properties);
}

static PFN_vkVoidFunction VKAPI_CALL LayerBaseGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return nullptr;
    }
    return layersvt::LayerBase::GetActiveLayer()->GetInstanceProcAddr(instance, name);
}

static PFN_vkVoidFunction VKAPI_CALL LayerBaseGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!layersvt::LayerBase::GetActiveLayer()) {
        return nullptr;
    }
    return layersvt::LayerBase::GetActiveLayer()->GetDeviceProcAddr(device, name);
}

}  // extern "C"

namespace layersvt {

LayerBase::LayerBase(const LayerManifest* manifest) : manifest_(manifest) {
    active_layer_ = this;
}

LayerBase::~LayerBase() {
    if (active_layer_ == this) {
        active_layer_ = nullptr;
    }
}

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
    if (result == VK_SUCCESS) {
        DispatchTableManager::Get().InitInstanceTable(*instance, get_instance_proc_addr);
        DeviceInstanceTracker::Get().EagerMapDevices(*instance, get_instance_proc_addr);
        PostCreateInstance(*instance, &modified_create_info);
    }
    return result;
}

void LayerBase::DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
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
    if (!create_info || !device) {
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
    if (result == VK_SUCCESS) {
        DispatchTableManager::Get().InitDeviceTable(*device, get_device_proc_addr);
        if (loader_callback) {
            DispatchTableManager::Get().SetDeviceLoaderDataCallback(*device, loader_callback);
        }
        OnDeviceCreated(*device, physical_device, &modified_create_info);
    }
    return result;
}

void LayerBase::DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    PreDestroyDevice(device, allocator);
    auto* table = DispatchTableManager::Get().GetDeviceDispatchTable(device);
    DispatchKey key = GetDispatchKey(device);
    if (table && table->DestroyDevice) {
        table->DestroyDevice(device, allocator);
    }
    PostDestroyDevice(device);
    DispatchTableManager::Get().DestroyDeviceTable(key);
}

VkResult LayerBase::EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                         VkExtensionProperties* properties) {
    if (!manifest_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return manifest_->EnumerateInstanceExtensionProperties(layer_name, property_count, properties);
}

VkResult LayerBase::EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) {
    if (!manifest_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return manifest_->EnumerateInstanceLayerProperties(property_count, properties);
}

VkResult LayerBase::EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                   VkLayerProperties* properties) {
    (void)physical_device;
    if (!manifest_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return manifest_->EnumerateDeviceLayerProperties(property_count, properties);
}

VkResult LayerBase::EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                       uint32_t* property_count, VkExtensionProperties* properties) {
    if (!manifest_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkEnumerateDeviceExtensionProperties downstream = nullptr;
    if (physical_device != VK_NULL_HANDLE) {
        VkInstance instance = DeviceInstanceTracker::Get().GetVkInstance(physical_device);
        if (instance != VK_NULL_HANDLE) {
            auto* table = DispatchTableManager::Get().GetInstanceDispatchTable(instance);
            if (table) {
                downstream = table->EnumerateDeviceExtensionProperties;
            }
        }
    }
    return manifest_->EnumerateDeviceExtensionProperties(physical_device, layer_name, property_count, properties, downstream);
}

void LayerBase::PreCreateInstance(VkInstanceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PostCreateInstance(VkInstance, const VkInstanceCreateInfo*) {}
void LayerBase::PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}
void LayerBase::PostDestroyInstance(VkInstance) {}

void LayerBase::PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::OnDeviceCreated(VkDevice, VkPhysicalDevice, const VkDeviceCreateInfo*) {}
void LayerBase::PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) {}
void LayerBase::PostDestroyDevice(VkDevice) {}

PFN_vkVoidFunction LayerBase::GetLayerSpecificInstanceFunction(const char*) {
    return nullptr;
}

PFN_vkVoidFunction LayerBase::GetLayerSpecificDeviceFunction(const char*) {
    return nullptr;
}

PFN_vkVoidFunction LayerBase::GetKnownInstanceFunction(const char* name) {
    if (!name) {
        return nullptr;
    }

    PFN_vkVoidFunction custom_func = GetLayerSpecificInstanceFunction(name);
    if (custom_func != nullptr) {
        return custom_func;
    }

    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseGetInstanceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseCreateInstance);
    if (std::strcmp(name, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseDestroyInstance);
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseEnumeratePhysicalDevices);
    if (std::strcmp(name, "vkEnumeratePhysicalDeviceGroups") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseEnumeratePhysicalDeviceGroups);
    if (std::strcmp(name, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseCreateDevice);
    if (manifest_) {
        if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseEnumerateInstanceExtensionProperties);
        if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseEnumerateInstanceLayerProperties);
        if (std::strcmp(name, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseEnumerateDeviceLayerProperties);
        if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseEnumerateDeviceExtensionProperties);
    }
    return nullptr;
}

PFN_vkVoidFunction LayerBase::GetKnownDeviceFunction(const char* name) {
    if (!name) {
        return nullptr;
    }

    PFN_vkVoidFunction custom_func = GetLayerSpecificDeviceFunction(name);
    if (custom_func != nullptr) {
        return custom_func;
    }

    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseGetDeviceProcAddr);
    if (std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerBaseDestroyDevice);
    return nullptr;
}

PFN_vkVoidFunction LayerBase::GetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) {
        return nullptr;
    }

    if (instance == VK_NULL_HANDLE) {
        bool is_global_command = (std::strcmp(name, "vkGetInstanceProcAddr") == 0) ||
                                 (std::strcmp(name, "vkCreateInstance") == 0) ||
                                 (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) ||
                                 (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0);
        if (!is_global_command) {
            return nullptr;
        }
    }

    PFN_vkVoidFunction func = GetKnownInstanceFunction(name);
    if (func != nullptr) {
        return func;
    }

    func = GetKnownDeviceFunction(name);
    if (func != nullptr) {
        return func;
    }

    if (instance == VK_NULL_HANDLE) {
        return nullptr;
    }

    auto* table = DispatchTableManager::Get().GetInstanceDispatchTable(instance);
    if (!table || !table->GetInstanceProcAddr) {
        return nullptr;
    }
    return table->GetInstanceProcAddr(instance, name);
}

PFN_vkVoidFunction LayerBase::GetDeviceProcAddr(VkDevice device, const char* name) {
    if (!name) {
        return nullptr;
    }

    if (device == VK_NULL_HANDLE) {
        return nullptr;
    }

    PFN_vkVoidFunction func = GetKnownDeviceFunction(name);
    if (func != nullptr) {
        return func;
    }

    auto* table = DispatchTableManager::Get().GetDeviceDispatchTable(device);
    if (!table || !table->GetDeviceProcAddr) {
        return nullptr;
    }
    return table->GetDeviceProcAddr(device, name);
}

}  // namespace layersvt
