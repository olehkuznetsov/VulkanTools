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
#include <optional>
#include <cstdint>

namespace layersvt {

/**
 * Declarative metadata describing a Vulkan layer's identity, versions,
 * exposed extensions, and optional tooling properties.
 */
struct LayerManifest {
    const char* layer_name = "";
    const char* description = "";
    uint32_t spec_version = VK_API_VERSION_1_3;
    uint32_t implementation_version = 1;
    std::vector<VkExtensionProperties> instance_extensions;
    std::vector<VkExtensionProperties> device_extensions;
    std::optional<VkPhysicalDeviceToolPropertiesEXT> tool_properties;

    /**
     * Converts manifest metadata into a standard VkLayerProperties structure.
     * Returns the populated VkLayerProperties instance.
     */
    [[nodiscard]] VkLayerProperties GetLayerProperties() const noexcept;
};

}  // namespace layersvt
