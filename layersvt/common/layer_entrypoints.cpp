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

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>
#include <cassert>

#ifndef VK_LAYER_EXPORT
#if defined(_WIN32)
// On Windows, layer DLL entry points are exported via .def module definition files.
// Omitting __declspec(dllexport) prevents MSVC C2375 linkage conflicts with Vulkan SDK headers.
#define VK_LAYER_EXPORT
#else
#define VK_LAYER_EXPORT __attribute__((visibility("default")))
#endif
#endif

extern "C" {

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* version_interface) {
    assert(version_interface != nullptr);
    assert(version_interface->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

    if (version_interface->loaderLayerInterfaceVersion >= 2) {
        version_interface->loaderLayerInterfaceVersion = 2;
        version_interface->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        version_interface->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
        version_interface->pfnGetPhysicalDeviceProcAddr = nullptr;
        return VK_SUCCESS;
    }

    if (version_interface->loaderLayerInterfaceVersion == 1) {
        version_interface->loaderLayerInterfaceVersion = 1;
        return VK_SUCCESS;
    }

    return VK_ERROR_INITIALIZATION_FAILED;
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* command_name) {
    return layersvt::LayerBase::GetInstanceProcAddr(instance, command_name);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char* command_name) {
    return layersvt::LayerBase::GetDeviceProcAddr(device, command_name);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* property_count, VkLayerProperties* properties) {
    return layersvt::LayerBase::EnumerateInstanceLayerProperties(property_count, properties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* layer_name, uint32_t* property_count, VkExtensionProperties* properties) {
    return layersvt::LayerBase::EnumerateInstanceExtensionProperties(
        layer_name, property_count, properties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physical_device, uint32_t* property_count, VkLayerProperties* properties) {
    return layersvt::LayerBase::EnumerateDeviceLayerProperties(
        physical_device, property_count, properties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical_device, const char* layer_name, uint32_t* property_count,
    VkExtensionProperties* properties) {
    return layersvt::LayerBase::EnumerateDeviceExtensionProperties(
        physical_device, layer_name, property_count, properties);
}

}  // extern "C"
