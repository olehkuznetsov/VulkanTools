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

#include "common/layer_base.h"
#include "common/layer_manifest.h"
#include <gtest/gtest.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

namespace layersvt {
namespace {

class EntrypointsTestLayer : public LayerBase {
   public:
    explicit EntrypointsTestLayer(const LayerManifest* manifest) : manifest_(manifest) {}
    ~EntrypointsTestLayer() override = default;

    [[nodiscard]] const LayerManifest* GetLayerManifest() const override { return manifest_; }

   private:
    const LayerManifest* manifest_;
};

TEST(LayerEntrypointsTest, ForwardingCalls) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Entrypoints",
    };
    EntrypointsTestLayer test_layer(&manifest);
    uint32_t property_count = 0;

    EXPECT_EQ(::vkEnumerateInstanceLayerProperties(&property_count, nullptr), VK_SUCCESS);
    EXPECT_EQ(::vkEnumerateInstanceExtensionProperties("VK_LAYER_TEST_Entrypoints", &property_count, nullptr), VK_SUCCESS);
    EXPECT_EQ(::vkEnumerateInstanceExtensionProperties(nullptr, &property_count, nullptr), VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(::vkEnumerateDeviceLayerProperties(VK_NULL_HANDLE, &property_count, nullptr), VK_SUCCESS);
    EXPECT_EQ(::vkEnumerateDeviceExtensionProperties(VK_NULL_HANDLE, nullptr, &property_count, nullptr), VK_SUCCESS);

    EXPECT_NE(::vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr"), nullptr);
    EXPECT_EQ(::vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkUnknownFunction"), nullptr);
    EXPECT_EQ(::vkGetDeviceProcAddr(VK_NULL_HANDLE, "vkUnknownFunction"), nullptr);

    VkNegotiateLayerInterface version_interface{
        .sType = LAYER_NEGOTIATE_INTERFACE_STRUCT,
        .pNext = nullptr,
        .loaderLayerInterfaceVersion = 2,
    };
    EXPECT_EQ(::vkNegotiateLoaderLayerInterfaceVersion(&version_interface), VK_SUCCESS);
    EXPECT_EQ(version_interface.loaderLayerInterfaceVersion, 2u);
    EXPECT_NE(version_interface.pfnGetInstanceProcAddr, nullptr);
    EXPECT_NE(version_interface.pfnGetDeviceProcAddr, nullptr);

    VkNegotiateLayerInterface version_one_interface{
        .sType = LAYER_NEGOTIATE_INTERFACE_STRUCT,
        .pNext = nullptr,
        .loaderLayerInterfaceVersion = 1,
    };
    EXPECT_EQ(::vkNegotiateLoaderLayerInterfaceVersion(&version_one_interface), VK_SUCCESS);
    EXPECT_EQ(version_one_interface.loaderLayerInterfaceVersion, 1u);

    VkNegotiateLayerInterface unsupported_version_interface{
        .sType = LAYER_NEGOTIATE_INTERFACE_STRUCT,
        .pNext = nullptr,
        .loaderLayerInterfaceVersion = 0,
    };
    EXPECT_EQ(::vkNegotiateLoaderLayerInterfaceVersion(&unsupported_version_interface), VK_ERROR_INITIALIZATION_FAILED);
}

}  // namespace
}  // namespace layersvt
