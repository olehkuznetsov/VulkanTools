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

#include "layer_test_helper.h"
#include "../debug_marker/debug_marker.h"
#include <vulkan/vulkan_core.h>
#include <gtest/gtest.h>
#include <stdlib.h>

static const char* kLayerName = "VK_LAYER_GOOGLE_DebugMarker";

class DebugMarkerTests : public VkTestFramework {
   public:
    ~DebugMarkerTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(DebugMarkerTests, CombinedTest) {
    TEST_DESCRIPTION("Combined test for DebugMarker layer");

    DebugMarker::Get().Clear();
    layer_test::VulkanInstanceBuilder inst_builder;
    inst_builder.AddExtension("VK_EXT_debug_utils");
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkInstance instance = inst_builder.GetInstance();
    EXPECT_NE(instance, VK_NULL_HANDLE);

    // Verify that GetInstanceProcAddr returns layer functions
    PFN_vkVoidFunction pfnSetDebugUtilsObjectNameEXT = vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");
    EXPECT_NE(pfnSetDebugUtilsObjectNameEXT, nullptr);

    PFN_vkVoidFunction pfnCmdDebugMarkerBeginEXT = vkGetInstanceProcAddr(instance, "vkCmdDebugMarkerBeginEXT");
    EXPECT_NE(pfnCmdDebugMarkerBeginEXT, nullptr);

    // 1. Set instance name
    DebugMarker::Get().SetDebugObjectName(0, VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance");

    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance"));

    // 2. Override new name
    DebugMarker::Get().SetDebugObjectName(0, VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed");
    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance"));

    // 3. Clear
    DebugMarker::Get().Clear();
    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
}

TEST_F(DebugMarkerTests, ManifestTest) {
    TEST_DESCRIPTION("Verify DebugMarker LayerManifest properties and extension enumeration via LayerBase");

    const auto* manifest = DebugMarker::Get().GetManifest();
    ASSERT_NE(manifest, nullptr);
    EXPECT_STREQ(manifest->GetConfig().layer_name, kLayerName);
    EXPECT_STREQ(manifest->GetConfig().description, "layer: DebugMarker");

    // Test EnumerateInstanceLayerProperties
    uint32_t property_count = 0;
    VkResult result = manifest->EnumerateInstanceLayerProperties(&property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    VkLayerProperties layer_properties{};
    result = manifest->EnumerateInstanceLayerProperties(&property_count, &layer_properties);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(layer_properties.layerName, kLayerName);

    // Test EnumerateDeviceLayerProperties
    property_count = 0;
    result = manifest->EnumerateDeviceLayerProperties(&property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    // Test EnumerateInstanceExtensionProperties
    property_count = 0;
    result = manifest->EnumerateInstanceExtensionProperties(kLayerName, &property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    std::vector<VkExtensionProperties> extensions(property_count);
    result = manifest->EnumerateInstanceExtensionProperties(kLayerName, &property_count, extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // Query non-matching layer name returns VK_ERROR_LAYER_NOT_PRESENT
    property_count = 0;
    result = manifest->EnumerateInstanceExtensionProperties("VK_LAYER_NONEXISTENT", &property_count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);

    // Test EnumerateDeviceExtensionProperties
    property_count = 0;
    result = manifest->EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, kLayerName, &property_count, nullptr, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    extensions.resize(property_count);
    result = manifest->EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, kLayerName, &property_count, extensions.data(), nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
}

TEST_F(DebugMarkerTests, LayerBaseLifecycleAndTrackerTest) {
    TEST_DESCRIPTION("Verify DebugMarker LayerBase inheritance and DeviceInstanceTracker integration");

    auto& marker = DebugMarker::Get();
    EXPECT_NE(marker.GetManifest(), nullptr);
    EXPECT_STREQ(marker.GetManifest()->GetConfig().layer_name, kLayerName);

    // Verify SetVkInstance / GetVkInstance forward to DeviceInstanceTracker
    VkPhysicalDevice mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x1234);
    VkInstance mock_instance = reinterpret_cast<VkInstance>(0x5678);

    marker.SetVkInstance(mock_physical_device, mock_instance);
    EXPECT_EQ(marker.GetVkInstance(mock_physical_device), mock_instance);

    marker.Clear();
    EXPECT_EQ(marker.GetVkInstance(mock_physical_device), VK_NULL_HANDLE);
}

TEST_F(DebugMarkerTests, PostDestroyDeviceCleanupTest) {
    TEST_DESCRIPTION("Verify that PostDestroyDevice cleans up tracked objects associated with that device");

    auto& marker = DebugMarker::Get();
    marker.Clear();

    VkDevice dev1 = reinterpret_cast<VkDevice>(0x1000);
    VkDevice dev2 = reinterpret_cast<VkDevice>(0x2000);

    marker.SetDebugObjectName((uint64_t)dev1, VK_OBJECT_TYPE_BUFFER, 0x1111, "Buffer1");
    marker.SetDebugObjectName((uint64_t)dev2, VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2");

    EXPECT_TRUE(marker.HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x1111, "Buffer1"));
    EXPECT_TRUE(marker.HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2"));

    // Destroy dev1 - should remove Buffer1 but keep Buffer2
    marker.PostDestroyDevice(dev1);

    EXPECT_FALSE(marker.HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x1111, "Buffer1"));
    EXPECT_TRUE(marker.HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2"));

    // Destroy dev2 - should remove Buffer2
    marker.PostDestroyDevice(dev2);
    EXPECT_FALSE(marker.HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2"));
}

TEST_F(DebugMarkerTests, TemplateMethodDispatchTest) {
    TEST_DESCRIPTION("Verify DebugMarker layer-specific hooks and LayerBase template method dispatching");

    auto& marker = DebugMarker::Get();

    // Layer-specific instance commands intercepted
    EXPECT_NE(marker.GetKnownInstanceFunction("vkCreateDebugUtilsMessengerEXT"), nullptr);
    EXPECT_NE(marker.GetKnownInstanceFunction("vkDestroyDebugUtilsMessengerEXT"), nullptr);
    EXPECT_NE(marker.GetKnownInstanceFunction("vkSubmitDebugUtilsMessageEXT"), nullptr);

    // Common lifecycle instance commands resolved via LayerBase fallback
    EXPECT_NE(marker.GetKnownInstanceFunction("vkCreateInstance"), nullptr);
    EXPECT_NE(marker.GetKnownInstanceFunction("vkDestroyInstance"), nullptr);
    EXPECT_NE(marker.GetKnownInstanceFunction("vkEnumeratePhysicalDevices"), nullptr);
    EXPECT_NE(marker.GetKnownInstanceFunction("vkEnumerateInstanceExtensionProperties"), nullptr);
    EXPECT_NE(marker.GetKnownInstanceFunction("vkCreateDevice"), nullptr);

    // Layer-specific device commands intercepted
    EXPECT_NE(marker.GetKnownDeviceFunction("vkCmdDebugMarkerBeginEXT"), nullptr);
    EXPECT_NE(marker.GetKnownDeviceFunction("vkCmdBeginDebugUtilsLabelEXT"), nullptr);
    EXPECT_NE(marker.GetKnownDeviceFunction("vkSetDebugUtilsObjectNameEXT"), nullptr);

    // Common lifecycle device commands resolved via LayerBase fallback
    EXPECT_EQ(marker.GetKnownDeviceFunction("vkCreateDevice"), nullptr);
    EXPECT_NE(marker.GetKnownDeviceFunction("vkDestroyDevice"), nullptr);
    EXPECT_NE(marker.GetKnownDeviceFunction("vkGetDeviceProcAddr"), nullptr);

    // Global commands resolvable with VK_NULL_HANDLE via GetInstanceProcAddr
    EXPECT_NE(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr"), nullptr);
    EXPECT_NE(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"), nullptr);
    EXPECT_NE(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"), nullptr);
    EXPECT_NE(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"), nullptr);

    // Non-global commands return nullptr when passed VK_NULL_HANDLE
    EXPECT_EQ(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDebugUtilsMessengerEXT"), nullptr);
    EXPECT_EQ(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCmdDebugMarkerBeginEXT"), nullptr);
    EXPECT_EQ(marker.GetDeviceProcAddr(VK_NULL_HANDLE, "vkCmdBeginDebugUtilsLabelEXT"), nullptr);
    EXPECT_EQ(marker.GetInstanceProcAddr(VK_NULL_HANDLE, "vkNonExistentCmd"), nullptr);
    EXPECT_EQ(marker.GetDeviceProcAddr(VK_NULL_HANDLE, "vkNonExistentCmd"), nullptr);

    // Non-global commands resolvable with valid instance/device handles
    VkInstance mock_instance = reinterpret_cast<VkInstance>(0x1234);
    VkDevice mock_device = reinterpret_cast<VkDevice>(0x5678);
    EXPECT_NE(marker.GetInstanceProcAddr(mock_instance, "vkCreateDebugUtilsMessengerEXT"), nullptr);
    EXPECT_NE(marker.GetInstanceProcAddr(mock_instance, "vkCmdDebugMarkerBeginEXT"), nullptr);
    EXPECT_NE(marker.GetDeviceProcAddr(mock_device, "vkCmdBeginDebugUtilsLabelEXT"), nullptr);
}
