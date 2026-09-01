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
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace layersvt {

/**
 * @brief Declarative metadata and extension property handler for a Vulkan layer.
 *
 * Centralizes the boilerplate for vkEnumerateInstanceLayerProperties,
 * vkEnumerateDeviceLayerProperties, vkEnumerateInstanceExtensionProperties,
 * vkEnumerateDeviceExtensionProperties, and vkGetPhysicalDeviceToolPropertiesEXT.
 *
 * Automatically handles the Android b/143293104 workaround and deduplication
 * when merging layer extensions with downstream driver extensions.
 */
class LayerManifest {
   public:
    struct Config {
        const char* layer_name = "";
        const char* description = "";
        uint32_t spec_version = VK_API_VERSION_1_3;
        uint32_t implementation_version = 1;
        std::vector<VkExtensionProperties> instance_extensions;
        std::vector<VkExtensionProperties> device_extensions;
        std::optional<VkPhysicalDeviceToolPropertiesEXT> tool_properties;
    };

    explicit LayerManifest(Config config);

    const Config& GetConfig() const { return config_; }

    VkResult EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) const;
    VkResult EnumerateDeviceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) const;
    VkResult EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                            VkLayerProperties* properties) const;

    VkResult EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                  VkExtensionProperties* properties) const;

    VkResult EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name, uint32_t* property_count,
                                                VkExtensionProperties* properties,
                                                PFN_vkEnumerateDeviceExtensionProperties downstream_function) const;

    VkResult GetPhysicalDeviceToolProperties(VkPhysicalDevice physical_device, uint32_t* tool_count,
                                             VkPhysicalDeviceToolPropertiesEXT* tool_properties,
                                             PFN_vkGetPhysicalDeviceToolPropertiesEXT downstream_function) const;

   private:
    Config config_;
    VkLayerProperties layer_properties_{};
};

}  // namespace layersvt
