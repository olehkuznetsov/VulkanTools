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

#include "layer_manifest.h"
#include <cstring>
#include <algorithm>

namespace layersvt {

namespace {

VkResult CopyExtensionProperties(const std::vector<VkExtensionProperties>& extensions, uint32_t* property_count,
                                 VkExtensionProperties* properties) {
    if (property_count == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const uint32_t total = static_cast<uint32_t>(extensions.size());
    if (properties == nullptr) {
        *property_count = total;
        return VK_SUCCESS;
    }

    const uint32_t copy_count = std::min(*property_count, total);
    if (copy_count > 0) {
        std::memcpy(properties, extensions.data(), copy_count * sizeof(VkExtensionProperties));
    }
    *property_count = copy_count;

    return (copy_count < total) ? VK_INCOMPLETE : VK_SUCCESS;
}

}  // namespace

LayerManifest::LayerManifest(Config config) : config_(std::move(config)) {
    std::memset(&layer_properties_, 0, sizeof(layer_properties_));
    if (config_.layer_name) {
        std::strncpy(layer_properties_.layerName, config_.layer_name, VK_MAX_EXTENSION_NAME_SIZE - 1);
    }
    if (config_.description) {
        std::strncpy(layer_properties_.description, config_.description, VK_MAX_DESCRIPTION_SIZE - 1);
    }
    layer_properties_.specVersion = config_.spec_version;
    layer_properties_.implementationVersion = config_.implementation_version;
}

VkResult LayerManifest::EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) const {
    if (property_count == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (properties == nullptr) {
        *property_count = 1;
        return VK_SUCCESS;
    }
    if (*property_count < 1) {
        return VK_INCOMPLETE;
    }
    *properties = layer_properties_;
    *property_count = 1;
    return VK_SUCCESS;
}

VkResult LayerManifest::EnumerateDeviceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) const {
    return EnumerateInstanceLayerProperties(property_count, properties);
}

VkResult LayerManifest::EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                       VkLayerProperties* properties) const {
    (void)physical_device;
    return EnumerateDeviceLayerProperties(property_count, properties);
}

VkResult LayerManifest::EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                             VkExtensionProperties* properties) const {
    if (property_count == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (layer_name && std::strcmp(layer_name, layer_properties_.layerName) != 0) {
        *property_count = 0;
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    return CopyExtensionProperties(config_.instance_extensions, property_count, properties);
}

VkResult LayerManifest::EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                           uint32_t* property_count, VkExtensionProperties* properties,
                                                           PFN_vkEnumerateDeviceExtensionProperties downstream_function) const {
    if (property_count == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // When explicitly querying this layer's device extensions:
    if (layer_name && std::strcmp(layer_name, layer_properties_.layerName) == 0) {
        return CopyExtensionProperties(config_.device_extensions, property_count, properties);
    }

    // If another layer is being queried, this layer doesn't handle it.
    if (layer_name && std::strcmp(layer_name, layer_properties_.layerName) != 0) {
        if (downstream_function) {
            return downstream_function(physical_device, layer_name, property_count, properties);
        }
        *property_count = 0;
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    // layer_name is nullptr: query downstream extensions and merge with layer device extensions
    const uint32_t layer_count = static_cast<uint32_t>(config_.device_extensions.size());
    if (properties == nullptr) {
        uint32_t driver_count = 0;
        VkResult result = downstream_function ? downstream_function(physical_device, nullptr, &driver_count, nullptr) : VK_SUCCESS;
        if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
            *property_count = driver_count + layer_count;
            return VK_SUCCESS;
        }
        return result;
    }

    uint32_t requested = *property_count;
    uint32_t driver_count = requested;
    VkResult result = VK_SUCCESS;
    if (downstream_function) {
        result = downstream_function(physical_device, nullptr, &driver_count, properties);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            return result;
        }
    } else {
        // When downstream_function is null (standalone layer or bottom of loader chain),
        // driver_count must be reset to 0 so we don't inspect uninitialized properties memory
        // with strcmp or skip layer extensions.
        driver_count = 0;
    }

    uint32_t current_count = driver_count;
    for (const auto& layer_extension : config_.device_extensions) {
        bool duplicate = false;
        for (uint32_t i = 0; i < driver_count; ++i) {
            if (std::strcmp(properties[i].extensionName, layer_extension.extensionName) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            if (current_count < requested) {
                properties[current_count] = layer_extension;
            }
            ++current_count;
        }
    }
    *property_count = std::min(current_count, requested);
    return (current_count > requested || result == VK_INCOMPLETE) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult LayerManifest::GetPhysicalDeviceToolProperties(VkPhysicalDevice physical_device, uint32_t* tool_count,
                                                        VkPhysicalDeviceToolPropertiesEXT* tool_properties,
                                                        PFN_vkGetPhysicalDeviceToolPropertiesEXT downstream_function) const {
    if (tool_count == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t downstream_count = 0;
    if (downstream_function) {
        VkResult result = downstream_function(physical_device, &downstream_count, nullptr);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            return result;
        }
    }

    const uint32_t layer_tool_count = config_.tool_properties.has_value() ? 1 : 0;
    const uint32_t total = downstream_count + layer_tool_count;

    if (tool_properties == nullptr) {
        *tool_count = total;
        return VK_SUCCESS;
    }

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools;
    if (downstream_function && downstream_count > 0) {
        tools.resize(downstream_count);
        VkResult result = downstream_function(physical_device, &downstream_count, tools.data());
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            return result;
        }
        tools.resize(downstream_count);
    }

    if (config_.tool_properties.has_value()) {
        tools.push_back(*config_.tool_properties);
    }

    const uint32_t copy_count = std::min(*tool_count, static_cast<uint32_t>(tools.size()));
    for (uint32_t i = 0; i < copy_count; ++i) {
        tool_properties[i] = tools[i];
    }
    *tool_count = copy_count;

    return (copy_count < tools.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

}  // namespace layersvt
