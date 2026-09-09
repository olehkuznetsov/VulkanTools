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
