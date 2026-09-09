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
#include "dispatch_downstream.h"
#include "dispatch_table_manager.h"

#include <cassert>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdlib.h>
#if !defined(NDEBUG)
#include <crtdbg.h>
#endif
#endif

namespace layersvt {

namespace {

inline void AssertLayerInitialized() {
    assert(LayerBase::Get() != nullptr && "LayerBase instance must be initialized");
}

bool IsGlobalCommand(const char* command_name) {
    return std::strcmp(command_name, "vkGetInstanceProcAddr") == 0 ||
           std::strcmp(command_name, "vkCreateInstance") == 0 ||
           std::strcmp(command_name, "vkEnumerateInstanceExtensionProperties") == 0 ||
           std::strcmp(command_name, "vkEnumerateInstanceLayerProperties") == 0 ||
           std::strcmp(command_name, "vkEnumerateInstanceVersion") == 0;
}

VkLayerInstanceCreateInfo* GetChainInfo(const VkInstanceCreateInfo& create_info, VkLayerFunction function) {
    auto* chain_info = static_cast<const VkLayerInstanceCreateInfo*>(create_info.pNext);
    while (chain_info && (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain_info->function != function)) {
        chain_info = static_cast<const VkLayerInstanceCreateInfo*>(chain_info->pNext);
    }
    return const_cast<VkLayerInstanceCreateInfo*>(chain_info);
}

VkLayerDeviceCreateInfo* GetChainInfo(const VkDeviceCreateInfo& create_info, VkLayerFunction function) {
    auto* chain_info = static_cast<const VkLayerDeviceCreateInfo*>(create_info.pNext);
    while (chain_info && (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain_info->function != function)) {
        chain_info = static_cast<const VkLayerDeviceCreateInfo*>(chain_info->pNext);
    }
    return const_cast<VkLayerDeviceCreateInfo*>(chain_info);
}

#if defined(_WIN32)
void InitPlatformErrorHandling() {
#if !defined(NDEBUG)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}
#endif

}  // namespace

LayerBase::LayerBase() {
#if defined(_WIN32)
    InitPlatformErrorHandling();
#endif
    layer_ = this;
}

LayerBase::~LayerBase() {
    if (layer_ == this) {
        layer_ = nullptr;
    }
}

VkInstance LayerBase::GetVkInstance(VkPhysicalDevice physical_device) {
    AssertLayerInitialized();
    return layer_->dispatch_table_manager_.GetVkInstance(physical_device);
}

VkuInstanceDispatchTable* LayerBase::GetInstanceDispatchTable(VkInstance instance) {
    AssertLayerInitialized();
    return layer_->dispatch_table_manager_.GetInstanceDispatchTable(instance);
}

VkuInstanceDispatchTable* LayerBase::GetInstanceDispatchTable(VkPhysicalDevice physical_device) {
    AssertLayerInitialized();
    return layer_->dispatch_table_manager_.GetInstanceDispatchTable(physical_device);
}

VkuDeviceDispatchTable* LayerBase::GetDeviceDispatchTable(const void* object) {
    AssertLayerInitialized();
    return layer_->dispatch_table_manager_.GetDeviceDispatchTable(object);
}

PFN_vkSetDeviceLoaderData LayerBase::GetDeviceLoaderDataCallback(VkDevice device) {
    AssertLayerInitialized();
    return layer_->dispatch_table_manager_.GetDeviceLoaderDataCallback(device);
}

VkResult LayerBase::CreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator,
                                   VkInstance* instance) {
    if (!create_info || !instance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    AssertLayerInitialized();

    VkInstanceCreateInfo modified_create_info = *create_info;
    VkLayerInstanceCreateInfo* chain_info = GetChainInfo(modified_create_info, VK_LAYER_LINK_INFO);
    if (!chain_info || !chain_info->u.pLayerInfo || !chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (create_instance == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LayerBase* layer = Get();
    layer->PreCreateInstance(&modified_create_info, allocator);

    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = create_instance(&modified_create_info, allocator, instance);
    if (result == VK_SUCCESS) {
        layer->dispatch_table_manager_.InitInstanceTable(*instance, get_instance_proc_addr);
        layer->PostCreateInstance(*instance, &modified_create_info, allocator);
    }
    return result;
}

void LayerBase::DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    if (instance == VK_NULL_HANDLE) {
        return;
    }

    AssertLayerInitialized();
    LayerBase* layer = Get();
    layer->PreDestroyInstance(instance, allocator);

    auto key = DispatchTableManager::GetDispatchKey(instance);
    DispatchDownstream<&VkuInstanceDispatchTable::DestroyInstance>(instance, allocator);
    layer->dispatch_table_manager_.DestroyInstanceTable(key);
}

VkResult LayerBase::CreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                 const VkAllocationCallbacks* allocator, VkDevice* device) {
    if (physical_device == VK_NULL_HANDLE || !create_info || !device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance instance = GetVkInstance(physical_device);
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceCreateInfo modified_create_info = *create_info;
    VkLayerDeviceCreateInfo* chain_info = GetChainInfo(modified_create_info, VK_LAYER_LINK_INFO);
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
    VkLayerDeviceCreateInfo* callback_info = GetChainInfo(modified_create_info, VK_LOADER_DATA_CALLBACK);
    if (callback_info && callback_info->u.pfnSetDeviceLoaderData) {
        loader_callback = callback_info->u.pfnSetDeviceLoaderData;
    }

    LayerBase* layer = Get();
    layer->PreCreateDevice(physical_device, &modified_create_info, allocator);

    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = create_device(physical_device, &modified_create_info, allocator, device);
    if (result == VK_SUCCESS) {
        layer->dispatch_table_manager_.InitDeviceTable(*device, get_device_proc_addr);
        if (loader_callback) {
            layer->dispatch_table_manager_.SetDeviceLoaderDataCallback(*device, loader_callback);
        }
        layer->PostCreateDevice(*device, physical_device, &modified_create_info, allocator);
    }
    return result;
}

void LayerBase::DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    AssertLayerInitialized();
    LayerBase* layer = Get();
    layer->PreDestroyDevice(device, allocator);

    auto key = DispatchTableManager::GetDispatchKey(device);
    DispatchDownstream<&VkuDeviceDispatchTable::DestroyDevice>(device, allocator);
    layer->dispatch_table_manager_.DestroyDeviceTable(key);
}

void LayerBase::PreCreateInstance(VkInstanceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PostCreateInstance(VkInstance, const VkInstanceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}

void LayerBase::PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PostCreateDevice(VkDevice, VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) {}

PFN_vkVoidFunction LayerBase::GetLayerInstanceCommand(const char*) { return nullptr; }

PFN_vkVoidFunction LayerBase::GetLayerDeviceCommand(const char*) { return nullptr; }

PFN_vkVoidFunction LayerBase::GetKnownInstanceCommand(const char* command_name) {
    assert(command_name != nullptr);
    AssertLayerInitialized();
    LayerBase* layer = Get();
    PFN_vkVoidFunction custom_command = layer->GetLayerInstanceCommand(command_name);
    if (custom_command != nullptr) {
        return custom_command;
    }

    if (std::strcmp(command_name, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(GetInstanceProcAddr);
    }
    if (std::strcmp(command_name, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
    }
    if (std::strcmp(command_name, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
    }
    if (std::strcmp(command_name, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(CreateDevice);
    }
    return nullptr;
}

PFN_vkVoidFunction LayerBase::GetKnownDeviceCommand(const char* command_name) {
    assert(command_name != nullptr);
    AssertLayerInitialized();
    LayerBase* layer = Get();
    PFN_vkVoidFunction custom_command = layer->GetLayerDeviceCommand(command_name);
    if (custom_command != nullptr) {
        return custom_command;
    }

    if (std::strcmp(command_name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(GetDeviceProcAddr);
    }
    if (std::strcmp(command_name, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice);
    }
    return nullptr;
}

PFN_vkVoidFunction VKAPI_CALL LayerBase::GetInstanceProcAddr(VkInstance instance, const char* command_name) {
    if (!command_name) {
        return nullptr;
    }

    AssertLayerInitialized();

    if (instance == VK_NULL_HANDLE) {
        if (!IsGlobalCommand(command_name)) {
            return nullptr;
        }
        return GetKnownInstanceCommand(command_name);
    }

    PFN_vkVoidFunction command = GetKnownInstanceCommand(command_name);
    if (command != nullptr) {
        return command;
    }

    command = GetKnownDeviceCommand(command_name);
    if (command != nullptr) {
        return command;
    }

    return DispatchDownstreamOr<&VkuInstanceDispatchTable::GetInstanceProcAddr>(nullptr, instance, command_name);
}

PFN_vkVoidFunction VKAPI_CALL LayerBase::GetDeviceProcAddr(VkDevice device, const char* command_name) {
    if (!command_name) {
        return nullptr;
    }

    if (device == VK_NULL_HANDLE) {
        return nullptr;
    }

    AssertLayerInitialized();
    PFN_vkVoidFunction command = GetKnownDeviceCommand(command_name);
    if (command != nullptr) {
        return command;
    }

    return DispatchDownstreamOr<&VkuDeviceDispatchTable::GetDeviceProcAddr>(nullptr, device, command_name);
}

}  // namespace layersvt
