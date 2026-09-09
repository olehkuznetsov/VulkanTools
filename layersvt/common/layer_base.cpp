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
#include "layer_manifest.h"

#include <algorithm>
#include <cassert>
#include <cstring>

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

template <typename T>
VkResult CopyEnumerationProperties(const std::vector<T>& items, uint32_t* property_count, T* properties) {
    assert(property_count != nullptr);

    const uint32_t total = static_cast<uint32_t>(items.size());
    if (properties == nullptr) {
        *property_count = total;
        return VK_SUCCESS;
    }

    const uint32_t copy_count = std::min(*property_count, total);
    if (copy_count > 0) {
        std::copy_n(items.begin(), copy_count, properties);
    }
    *property_count = copy_count;

    return (copy_count < total) ? VK_INCOMPLETE : VK_SUCCESS;
}

}  // namespace

LayerBase::LayerBase() {
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

VkResult LayerBase::EnumeratePhysicalDevices(VkInstance instance, uint32_t* physical_device_count,
                                             VkPhysicalDevice* physical_devices) {
    VkResult result = DispatchDownstreamOr<&VkuInstanceDispatchTable::EnumeratePhysicalDevices>(
        VK_ERROR_INITIALIZATION_FAILED, instance, physical_device_count, physical_devices);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && physical_device_count != nullptr &&
        physical_devices != nullptr) {
        LayerBase* layer = Get();
        layer->dispatch_table_manager_.RegisterPhysicalDevices(physical_devices, *physical_device_count, instance);
    }
    return result;
}

VkResult LayerBase::EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* physical_device_group_count,
                                                  VkPhysicalDeviceGroupProperties* physical_device_group_properties) {
    VkResult result = DispatchDownstreamOr<&VkuInstanceDispatchTable::EnumeratePhysicalDeviceGroups>(
        [&] {
            return DispatchDownstreamOr<&VkuInstanceDispatchTable::EnumeratePhysicalDeviceGroupsKHR>(
                VK_ERROR_INITIALIZATION_FAILED, instance, physical_device_group_count,
                physical_device_group_properties);
        },
        instance, physical_device_group_count, physical_device_group_properties);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && physical_device_group_count != nullptr &&
        physical_device_group_properties != nullptr) {
        LayerBase* layer = Get();
        for (uint32_t i = 0; i < *physical_device_group_count; ++i) {
            assert(physical_device_group_properties[i].physicalDeviceCount <= VK_MAX_DEVICE_GROUP_SIZE);
            const uint32_t device_count = physical_device_group_properties[i].physicalDeviceCount;
            layer->dispatch_table_manager_.RegisterPhysicalDevices(physical_device_group_properties[i].physicalDevices,
                                                                  device_count, instance);
        }
    }
    return result;
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

VkResult LayerBase::EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                         VkExtensionProperties* properties) {
    assert(property_count != nullptr);
    AssertLayerInitialized();

    LayerBase* layer = Get();
    const LayerManifest* manifest = layer->GetLayerManifest();
    assert(manifest != nullptr);
    const char* my_layer_name = (manifest->layer_name != nullptr) ? manifest->layer_name : "";

    if (layer_name == nullptr || my_layer_name[0] == '\0' || std::strcmp(layer_name, my_layer_name) != 0) {
        *property_count = 0;
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    std::vector<VkExtensionProperties> extensions = manifest->instance_extensions;
    return CopyEnumerationProperties(extensions, property_count, properties);
}

VkResult LayerBase::EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) {
    assert(property_count != nullptr);
    AssertLayerInitialized();

    LayerBase* layer = Get();
    const LayerManifest* manifest = layer->GetLayerManifest();
    assert(manifest != nullptr);

    if (properties == nullptr) {
        *property_count = 1;
        return VK_SUCCESS;
    }

    if (*property_count < 1) {
        return VK_INCOMPLETE;
    }

    *properties = manifest->GetLayerProperties();
    *property_count = 1;
    return VK_SUCCESS;
}

VkResult LayerBase::EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                   VkLayerProperties* properties) {
    (void)physical_device;
    return EnumerateInstanceLayerProperties(property_count, properties);
}

VkResult LayerBase::EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                       uint32_t* property_count, VkExtensionProperties* properties) {
    assert(property_count != nullptr);
    AssertLayerInitialized();

    LayerBase* layer = Get();
    const LayerManifest* manifest = layer->GetLayerManifest();
    assert(manifest != nullptr);
    const char* my_layer_name = (manifest->layer_name != nullptr) ? manifest->layer_name : "";

    // When explicitly querying this layer's device extensions:
    if (layer_name != nullptr && my_layer_name[0] != '\0' && std::strcmp(layer_name, my_layer_name) == 0) {
        std::vector<VkExtensionProperties> extensions = manifest->device_extensions;
        layer->ProcessDeviceExtensions(physical_device, layer_name, extensions);
        return CopyEnumerationProperties(extensions, property_count, properties);
    }

    PFN_vkEnumerateDeviceExtensionProperties downstream_function = nullptr;
    if (physical_device != VK_NULL_HANDLE) {
        auto* table = GetInstanceDispatchTable(physical_device);
        if (table != nullptr) {
            downstream_function = table->EnumerateDeviceExtensionProperties;
        }
    }

    // If another layer is being queried, forward downstream or return VK_ERROR_LAYER_NOT_PRESENT
    if (layer_name != nullptr) {
        if (downstream_function) {
            return downstream_function(physical_device, layer_name, property_count, properties);
        }
        *property_count = 0;
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    // layer_name is nullptr: query downstream extensions and merge with layer device extensions
    std::vector<VkExtensionProperties> extensions;
    if (downstream_function) {
        uint32_t downstream_count = 0;
        VkResult result = downstream_function(physical_device, nullptr, &downstream_count, nullptr);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            return result;
        }
        if (downstream_count > 0) {
            uint32_t allocated_count = downstream_count;
            extensions.resize(allocated_count);
            result = downstream_function(physical_device, nullptr, &downstream_count, extensions.data());
            if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
                return result;
            }
            extensions.resize(std::min(downstream_count, allocated_count));
        }
    }

    for (const auto& layer_extension : manifest->device_extensions) {
        bool duplicate = false;
        for (const auto& existing : extensions) {
            if (std::strcmp(existing.extensionName, layer_extension.extensionName) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            extensions.push_back(layer_extension);
        }
    }

    layer->ProcessDeviceExtensions(physical_device, layer_name, extensions);

    return CopyEnumerationProperties(extensions, property_count, properties);
}

VkResult LayerBase::GetPhysicalDeviceToolProperties(VkPhysicalDevice physical_device, uint32_t* tool_count,
                                                    VkPhysicalDeviceToolPropertiesEXT* tool_properties) {
    assert(tool_count != nullptr);
    AssertLayerInitialized();

    LayerBase* layer = Get();
    const LayerManifest* manifest = layer->GetLayerManifest();
    assert(manifest != nullptr);

    PFN_vkGetPhysicalDeviceToolPropertiesEXT downstream_function = nullptr;
    if (physical_device != VK_NULL_HANDLE) {
        auto* table = GetInstanceDispatchTable(physical_device);
        if (table != nullptr) {
            downstream_function = table->GetPhysicalDeviceToolPropertiesEXT;
            if (!downstream_function) {
                downstream_function = table->GetPhysicalDeviceToolProperties;
            }
        }
    }

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools;
    if (downstream_function) {
        uint32_t downstream_count = 0;
        VkResult result = downstream_function(physical_device, &downstream_count, nullptr);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            return result;
        }
        if (downstream_count > 0) {
            uint32_t allocated_count = downstream_count;
            tools.resize(allocated_count);
            for (auto& tool : tools) {
                tool.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT;
                tool.pNext = nullptr;
            }
            result = downstream_function(physical_device, &downstream_count, tools.data());
            if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
                return result;
            }
            tools.resize(std::min(downstream_count, allocated_count));
        }
    }

    if (manifest->tool_properties.has_value()) {
        tools.push_back(*manifest->tool_properties);
    }

    return CopyEnumerationProperties(tools, tool_count, tool_properties);
}

void LayerBase::ProcessDeviceExtensions(VkPhysicalDevice, const char*, std::vector<VkExtensionProperties>&) const {}

void LayerBase::PreCreateInstance(VkInstanceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PostCreateInstance(VkInstance, const VkInstanceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}

void LayerBase::PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PostCreateDevice(VkDevice, VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*) {}
void LayerBase::PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) {}

const LayerManifest* LayerBase::GetLayerManifest() const {
    static const LayerManifest kDefaultManifest{};
    return &kDefaultManifest;
}

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
    if (std::strcmp(command_name, "vkEnumeratePhysicalDevices") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumeratePhysicalDevices);
    }
    if (std::strcmp(command_name, "vkEnumeratePhysicalDeviceGroups") == 0 ||
        std::strcmp(command_name, "vkEnumeratePhysicalDeviceGroupsKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumeratePhysicalDeviceGroups);
    }
    if (std::strcmp(command_name, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(CreateDevice);
    }
    if (std::strcmp(command_name, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(command_name, "vkEnumerateInstanceLayerProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceLayerProperties);
    }
    if (std::strcmp(command_name, "vkEnumerateDeviceLayerProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceLayerProperties);
    }
    if (std::strcmp(command_name, "vkEnumerateDeviceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceExtensionProperties);
    }
    if (std::strcmp(command_name, "vkGetPhysicalDeviceToolPropertiesEXT") == 0 ||
        std::strcmp(command_name, "vkGetPhysicalDeviceToolProperties") == 0) {
        const LayerManifest* manifest = layer->GetLayerManifest();
        if (manifest != nullptr && manifest->tool_properties.has_value()) {
            return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceToolProperties);
        }
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
