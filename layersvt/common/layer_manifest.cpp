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
#include <cassert>
#include <cstring>
namespace layersvt {

VkLayerProperties LayerManifest::GetLayerProperties() const noexcept {
    assert(layer_name != nullptr);
    assert(description != nullptr);
    VkLayerProperties properties{};
    std::strncpy(properties.layerName, layer_name, sizeof(properties.layerName) - 1);
    properties.layerName[sizeof(properties.layerName) - 1] = '\0';
    std::strncpy(properties.description, description, sizeof(properties.description) - 1);
    properties.description[sizeof(properties.description) - 1] = '\0';
    properties.specVersion = spec_version;
    properties.implementationVersion = implementation_version;
    return properties;
}

}  // namespace layersvt
