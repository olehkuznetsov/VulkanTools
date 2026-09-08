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

#include "common/layer_manifest.h"
#include "common/layer_base.h"
#include "layer_base_test_peer.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace layersvt;

TEST(LayerManifestTest, LayerProperties) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .description = "Sample Test Layer",
        .spec_version = VK_API_VERSION_1_3,
        .implementation_version = 42,
    };
    VkLayerProperties layer_properties = manifest.GetLayerProperties();

    EXPECT_STREQ(layer_properties.layerName, "VK_LAYER_TEST_Sample");
    EXPECT_STREQ(layer_properties.description, "Sample Test Layer");
    EXPECT_EQ(layer_properties.specVersion, VK_API_VERSION_1_3);
    EXPECT_EQ(layer_properties.implementationVersion, 42u);
}

class ManifestTestLayer : public LayerBase {
   public:
    explicit ManifestTestLayer(const LayerManifest* manifest) : manifest_(manifest) {}
    const LayerManifest* GetLayerManifest() const override { return manifest_; }

   private:
    const LayerManifest* manifest_;
};

TEST(LayerBaseEnumerationTest, LayerProperties) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .description = "Sample Test Layer",
        .spec_version = VK_API_VERSION_1_3,
        .implementation_version = 42,
    };
    ManifestTestLayer layer(&manifest);

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateInstanceLayerProperties(&count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    VkLayerProperties layer_properties{};
    result = LayerBaseTestPeer::EnumerateInstanceLayerProperties(&count, &layer_properties);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(layer_properties.layerName, "VK_LAYER_TEST_Sample");
    EXPECT_STREQ(layer_properties.description, "Sample Test Layer");
    EXPECT_EQ(layer_properties.specVersion, VK_API_VERSION_1_3);
    EXPECT_EQ(layer_properties.implementationVersion, 42u);
}

TEST(LayerBaseEnumerationTest, InstanceExtensions) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .instance_extensions = {
            {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION},
        },
    };
    ManifestTestLayer layer(&manifest);

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_TEST_Sample", &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkExtensionProperties> extensions(count);
    result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_TEST_Sample", &count, extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    EXPECT_EQ(extensions[0].specVersion, static_cast<uint32_t>(VK_EXT_DEBUG_UTILS_SPEC_VERSION));

    // Querying with nullptr or unknown layer name must return VK_ERROR_LAYER_NOT_PRESENT per LLP_LAYER_15
    count = 5;
    result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(count, 0u);

    count = 5;
    result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_UNKNOWN", &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(count, 0u);
}

TEST(LayerBaseEnumerationTest, DeviceExtensionsDownstreamMerge) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .device_extensions = {
            {"VK_EXT_custom_layer_extension", 1},
        },
    };
    ManifestTestLayer layer(&manifest);

    // Mock downstream driver enumeration that returns VK_KHR_swapchain
    auto mock_downstream = [](VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties* properties) -> VkResult {
        if (!properties) {
            *count = 1;
            return VK_SUCCESS;
        }
        std::strncpy(properties[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        properties[0].specVersion = VK_KHR_SWAPCHAIN_SPEC_VERSION;
        *count = 1;
        return VK_SUCCESS;
    };

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, nullptr, mock_downstream);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);  // 1 from layer + 1 from driver

    std::vector<VkExtensionProperties> merged(count);
    result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, merged.data(), mock_downstream);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);

    bool has_layer_extension = false;
    bool has_driver_extension = false;
    for (const auto& extension : merged) {
        if (std::strcmp(extension.extensionName, "VK_EXT_custom_layer_extension") == 0) {
            has_layer_extension = true;
        }
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            has_driver_extension = true;
        }
    }
    EXPECT_TRUE(has_layer_extension);
    EXPECT_TRUE(has_driver_extension);
}

TEST(LayerBaseEnumerationTest, DeviceExtensionsDownstreamIncomplete) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
    };
    ManifestTestLayer layer(&manifest);

    // Mock downstream driver returning 5 extensions
    auto mock_downstream = [](VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties* properties) -> VkResult {
        if (!properties) {
            *count = 5;
            return VK_SUCCESS;
        }
        uint32_t to_copy = std::min(*count, 5u);
        for (uint32_t i = 0; i < to_copy; ++i) {
            std::snprintf(properties[i].extensionName, VK_MAX_EXTENSION_NAME_SIZE, "VK_EXT_driver_%u", i);
            properties[i].specVersion = 1;
        }
        *count = to_copy;
        return (to_copy < 5u) ? VK_INCOMPLETE : VK_SUCCESS;
    };

    uint32_t count = 1;
    VkExtensionProperties property{};
    VkResult result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, &property, mock_downstream);
    EXPECT_EQ(result, VK_INCOMPLETE);
    EXPECT_EQ(count, 1u);
}

TEST(LayerBaseEnumerationTest, ToolPropertiesMerge) {
    VkPhysicalDeviceToolPropertiesEXT layer_tool_properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
                                                               nullptr,
                                                               "CommonLayerTool",
                                                               "1.0",
                                                               VK_TOOL_PURPOSE_PROFILING_BIT_EXT,
                                                               "Diagnostic tool description",
                                                               "CommonLayer"};

    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .tool_properties = layer_tool_properties,
    };
    ManifestTestLayer layer(&manifest);

    // Mock downstream reporting 1 driver tool
    auto mock_downstream_tool = [](VkPhysicalDevice, uint32_t* count, VkPhysicalDeviceToolPropertiesEXT* properties) -> VkResult {
        if (!properties) {
            *count = 1;
            return VK_SUCCESS;
        }
        EXPECT_EQ(properties[0].sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT);
        EXPECT_EQ(properties[0].pNext, nullptr);
        std::strncpy(properties[0].name, "DriverTool", VK_MAX_EXTENSION_NAME_SIZE);
        *count = 1;
        return VK_SUCCESS;
    };

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::GetPhysicalDeviceToolProperties(nullptr, &count, nullptr, mock_downstream_tool);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools(count);
    result = LayerBaseTestPeer::GetPhysicalDeviceToolProperties(nullptr, &count, tools.data(), mock_downstream_tool);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(tools[0].name, "DriverTool");
    EXPECT_STREQ(tools[1].name, "CommonLayerTool");
}

TEST(LayerBaseEnumerationTest, DeviceLayerPropertiesOverload) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
    };
    ManifestTestLayer layer(&manifest);

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateDeviceLayerProperties(
        reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x123)), &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);
}

TEST(LayerBaseEnumerationTest, DeviceExtensionsNullDownstream) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .device_extensions = {
            {"VK_EXT_standalone_extension", 1},
        },
    };
    ManifestTestLayer layer(&manifest);

    uint32_t count = 0;
    EXPECT_EQ(LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, nullptr, nullptr), VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkExtensionProperties> extensions(count);
    EXPECT_EQ(LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, extensions.data(), nullptr), VK_SUCCESS);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(extensions[0].extensionName, "VK_EXT_standalone_extension");
}

TEST(LayerBaseEnumerationTest, QueryDifferentLayerName) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
    };
    ManifestTestLayer layer(&manifest);

    uint32_t count = 5;
    EXPECT_EQ(LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_OTHER", &count, nullptr), VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(count, 0u);

    count = 5;
    EXPECT_EQ(LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, "VK_LAYER_OTHER", &count, nullptr, nullptr),
              VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(count, 0u);
}

TEST(LayerBaseEnumerationTest, DeviceExtensionsMatchingLayerName) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
        .device_extensions = {
            {"VK_EXT_custom_ext1", 1},
            {"VK_EXT_custom_ext2", 2},
        },
    };
    ManifestTestLayer layer(&manifest);

    // 1. Query count with matching layer name (returns layer's own device extension count)
    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(
        nullptr, "VK_LAYER_TEST_Sample", &count, nullptr, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);

    // 2. Query properties buffer with sufficient space
    std::vector<VkExtensionProperties> extensions(count);
    result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(
        nullptr, "VK_LAYER_TEST_Sample", &count, extensions.data(), nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);
    EXPECT_STREQ(extensions[0].extensionName, "VK_EXT_custom_ext1");
    EXPECT_STREQ(extensions[1].extensionName, "VK_EXT_custom_ext2");

    // 3. Query properties buffer with insufficient space (returns VK_INCOMPLETE)
    count = 1;
    VkExtensionProperties single_extension{};
    result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(
        nullptr, "VK_LAYER_TEST_Sample", &count, &single_extension, nullptr);
    EXPECT_EQ(result, VK_INCOMPLETE);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(single_extension.extensionName, "VK_EXT_custom_ext1");
}

TEST(LayerBaseEnumerationTest, DeviceExtensionsForwardDifferentLayerName) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
    };
    ManifestTestLayer layer(&manifest);

    auto mock_downstream = [](VkPhysicalDevice, const char* layer_name, uint32_t* count,
                              VkExtensionProperties* properties) -> VkResult {
        if (std::strcmp(layer_name, "VK_LAYER_DOWNSTREAM") == 0) {
            if (!properties) {
                *count = 1;
                return VK_SUCCESS;
            }
            std::strncpy(properties[0].extensionName, "VK_EXT_downstream_ext", VK_MAX_EXTENSION_NAME_SIZE);
            *count = 1;
            return VK_SUCCESS;
        }
        return VK_ERROR_LAYER_NOT_PRESENT;
    };

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(
        nullptr, "VK_LAYER_DOWNSTREAM", &count, nullptr, mock_downstream);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkExtensionProperties> extensions(count);
    result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(
        nullptr, "VK_LAYER_DOWNSTREAM", &count, extensions.data(), mock_downstream);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(extensions[0].extensionName, "VK_EXT_downstream_ext");
}

TEST(LayerBaseEnumerationTest, ToolPropertiesErrorPropagation) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Sample",
    };
    ManifestTestLayer layer(&manifest);

    auto error_downstream_tool = [](VkPhysicalDevice, uint32_t*, VkPhysicalDeviceToolPropertiesEXT*) -> VkResult {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    };

    uint32_t count = 0;
    EXPECT_EQ(LayerBaseTestPeer::GetPhysicalDeviceToolProperties(nullptr, &count, nullptr, error_downstream_tool),
              VK_ERROR_OUT_OF_HOST_MEMORY);
}

TEST(LayerBaseHooksTest, ProcessDeviceExtensionsFiltering) {
    class FilteringTestLayer : public LayerBase {
       public:
        explicit FilteringTestLayer(const LayerManifest* manifest) : manifest_(manifest) {}
        const LayerManifest* GetLayerManifest() const override { return manifest_; }

       protected:
        void ProcessDeviceExtensions(VkPhysicalDevice, const char* layer_name,
                                     std::vector<VkExtensionProperties>& extensions) const override {
            if (layer_name == nullptr) {
                std::erase_if(extensions, [](const VkExtensionProperties& extension) {
                    return std::strcmp(extension.extensionName, "VK_EXT_disallowed") == 0;
                });
            }
        }

       private:
        const LayerManifest* manifest_;
    };

    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Filtering",
        .device_extensions = {
            {"VK_EXT_allowed_1", 1},
            {"VK_EXT_disallowed", 1},
            {"VK_EXT_allowed_2", 1},
        },
    };
    FilteringTestLayer layer(&manifest);

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, nullptr, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);

    std::vector<VkExtensionProperties> extensions(count);
    result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, extensions.data(), nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);
    EXPECT_STREQ(extensions[0].extensionName, "VK_EXT_allowed_1");
    EXPECT_STREQ(extensions[1].extensionName, "VK_EXT_allowed_2");
}

TEST(LayerBaseHooksTest, ProcessInstanceExtensionsAugmenting) {
    class AugmentingTestLayer : public LayerBase {
       public:
        explicit AugmentingTestLayer(const LayerManifest* manifest) : manifest_(manifest) {}
        const LayerManifest* GetLayerManifest() const override { return manifest_; }

       protected:
        void ProcessInstanceExtensions(const char* layer_name,
                                       std::vector<VkExtensionProperties>& extensions) const override {
            if (layer_name == nullptr || std::strcmp(layer_name, "VK_LAYER_TEST_Augmenting") == 0) {
                VkExtensionProperties dynamic_extension{};
                std::strncpy(dynamic_extension.extensionName, "VK_EXT_dynamic_instance_ext", VK_MAX_EXTENSION_NAME_SIZE);
                dynamic_extension.specVersion = 2;
                extensions.push_back(dynamic_extension);
            }
        }

       private:
        const LayerManifest* manifest_;
    };

    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Augmenting",
        .instance_extensions = {
            {"VK_EXT_static_instance_ext", 1},
        },
    };
    AugmentingTestLayer layer(&manifest);

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_TEST_Augmenting", &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);

    std::vector<VkExtensionProperties> extensions(count);
    result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_TEST_Augmenting", &count, extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);
    EXPECT_STREQ(extensions[0].extensionName, "VK_EXT_static_instance_ext");
    EXPECT_STREQ(extensions[1].extensionName, "VK_EXT_dynamic_instance_ext");
}

TEST(LayerBaseHooksTest, ProcessToolPropertiesCustomizing) {
    class ToolCustomizingLayer : public LayerBase {
       public:
        explicit ToolCustomizingLayer(const LayerManifest* manifest) : manifest_(manifest) {}
        const LayerManifest* GetLayerManifest() const override { return manifest_; }

       protected:
        void ProcessToolProperties(VkPhysicalDevice,
                                   std::vector<VkPhysicalDeviceToolPropertiesEXT>& tools) const override {
            for (auto& tool : tools) {
                std::strncpy(tool.description, "Customized Description", VK_MAX_DESCRIPTION_SIZE);
            }
        }

       private:
        const LayerManifest* manifest_;
    };

    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Tool",
        .tool_properties = VkPhysicalDeviceToolPropertiesEXT{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
            .pNext = nullptr,
            .name = "TestTool",
            .version = "1.0",
            .purposes = VK_TOOL_PURPOSE_PROFILING_BIT_EXT,
            .description = "Original Description",
            .layer = "VK_LAYER_TEST_Tool",
        },
    };
    ToolCustomizingLayer layer(&manifest);

    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::GetPhysicalDeviceToolProperties(nullptr, &count, nullptr, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools(count);
    result = LayerBaseTestPeer::GetPhysicalDeviceToolProperties(nullptr, &count, tools.data(), nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(tools[0].description, "Customized Description");
}

TEST(LayerBaseTest, GetKnownCommandsCommonWithManifest) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Common",
    };
    ManifestTestLayer layer(&manifest);

    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumerateInstanceExtensionProperties"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumerateInstanceLayerProperties"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumerateDeviceLayerProperties"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumerateDeviceExtensionProperties"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkEnumerateDeviceLayerProperties"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkEnumerateDeviceExtensionProperties"), nullptr);

    // Without tool_properties in manifest, tooling functions return nullptr
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetPhysicalDeviceToolPropertiesEXT"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetPhysicalDeviceToolProperties"), nullptr);
}

TEST(LayerBaseTest, GetKnownCommandsWithToolProperties) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Common",
        .tool_properties = VkPhysicalDeviceToolPropertiesEXT{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
            .pNext = nullptr,
            .name = "VK_LAYER_TEST_Common",
            .version = "1",
            .purposes = VK_TOOL_PURPOSE_TRACING_BIT_EXT,
            .description = "Test layer",
            .layer = "VK_LAYER_TEST_Common",
        },
    };
    ManifestTestLayer layer(&manifest);

    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetPhysicalDeviceToolPropertiesEXT"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetPhysicalDeviceToolProperties"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkGetPhysicalDeviceToolPropertiesEXT"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkGetPhysicalDeviceToolProperties"), nullptr);
}

TEST(LayerBaseTest, GetPhysicalDeviceToolPropertiesDispatch) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Common",
        .tool_properties = VkPhysicalDeviceToolPropertiesEXT{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
            .pNext = nullptr,
            .name = "VK_LAYER_TEST_Common",
            .version = "1",
            .purposes = VK_TOOL_PURPOSE_TRACING_BIT_EXT,
            .description = "Test layer",
            .layer = "VK_LAYER_TEST_Common",
        },
    };
    ManifestTestLayer layer(&manifest);

    // Test with null physical device (no downstream lookup)
    uint32_t count = 0;
    VkResult result = LayerBaseTestPeer::GetPhysicalDeviceToolProperties(VK_NULL_HANDLE, &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    ASSERT_EQ(count, 1u);

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools(count);
    result = LayerBaseTestPeer::GetPhysicalDeviceToolProperties(VK_NULL_HANDLE, &count, tools.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(tools[0].name, "VK_LAYER_TEST_Common");
}

TEST(LayerBaseEnumerationTest, EmptyExtensionsWithBuffer) {
    LayerManifest manifest{
        .layer_name = "VK_LAYER_TEST_Empty",
    };
    ManifestTestLayer layer(&manifest);

    uint32_t count = 5;
    VkExtensionProperties properties[5]{};
    VkResult result = LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_TEST_Empty", &count, properties);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 0u);

    count = 5;
    result = LayerBaseTestPeer::EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, properties, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 0u);
}

