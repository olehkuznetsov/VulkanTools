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

namespace layersvt {
class LayerBaseTestPeer {
   public:
    static PFN_vkVoidFunction GetKnownInstanceCommand(const char* name) {
        return LayerBase::GetKnownInstanceCommand(name);
    }
    static PFN_vkVoidFunction GetKnownDeviceCommand(const char* name) {
        return LayerBase::GetKnownDeviceCommand(name);
    }
    static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* name) {
        return LayerBase::GetInstanceProcAddr(instance, name);
    }
    static PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* name) {
        return LayerBase::GetDeviceProcAddr(device, name);
    }
    static VkResult EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* property_count,
                                                         VkExtensionProperties* properties) {
        return LayerBase::EnumerateInstanceExtensionProperties(layer_name, property_count, properties);
    }
    static VkResult EnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) {
        return LayerBase::EnumerateInstanceLayerProperties(property_count, properties);
    }
    static VkResult EnumerateDeviceLayerProperties(VkPhysicalDevice physical_device, uint32_t* property_count,
                                                   VkLayerProperties* properties) {
        return LayerBase::EnumerateDeviceLayerProperties(physical_device, property_count, properties);
    }
    static VkResult EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                       uint32_t* property_count, VkExtensionProperties* properties) {
        return LayerBase::EnumerateDeviceExtensionProperties(physical_device, layer_name, property_count, properties);
    }
    static DeviceInstanceTracker& GetDeviceTracker(LayerBase& layer) {
        return layer.GetDeviceTracker();
    }
};
}  // namespace layersvt

static const char* kLayerName = "VK_LAYER_GOOGLE_DebugMarker";

class DebugMarkerTests : public VkTestFramework {
   public:
    ~DebugMarkerTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};

   protected:
    void SetUp() override {
        VkTestFramework::SetUp();
        layer_test::ResetLayer<DebugMarker>();
    }
};

TEST_F(DebugMarkerTests, CombinedTest) {
    TEST_DESCRIPTION("Combined test for DebugMarker layer");

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

    // 3. Reset
    layer_test::ResetLayer<DebugMarker>();
    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
}

TEST_F(DebugMarkerTests, ManifestTest) {
    TEST_DESCRIPTION("Verify DebugMarker LayerManifest properties and extension enumeration via LayerBase");

    // Test EnumerateInstanceLayerProperties
    uint32_t property_count = 0;
    VkResult result = layersvt::LayerBaseTestPeer::EnumerateInstanceLayerProperties(&property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    VkLayerProperties layer_properties{};
    result = layersvt::LayerBaseTestPeer::EnumerateInstanceLayerProperties(&property_count, &layer_properties);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(layer_properties.layerName, kLayerName);
    EXPECT_STREQ(layer_properties.description, "layer: DebugMarker");

    // Test EnumerateDeviceLayerProperties
    property_count = 0;
    result = layersvt::LayerBaseTestPeer::EnumerateDeviceLayerProperties(VK_NULL_HANDLE, &property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    // Test EnumerateInstanceExtensionProperties
    property_count = 0;
    result = layersvt::LayerBaseTestPeer::EnumerateInstanceExtensionProperties(kLayerName, &property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    std::vector<VkExtensionProperties> extensions(property_count);
    result = layersvt::LayerBaseTestPeer::EnumerateInstanceExtensionProperties(kLayerName, &property_count, extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // Query non-matching layer name returns VK_ERROR_LAYER_NOT_PRESENT
    property_count = 0;
    result = layersvt::LayerBaseTestPeer::EnumerateInstanceExtensionProperties("VK_LAYER_NONEXISTENT", &property_count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);

    // Test EnumerateDeviceExtensionProperties
    property_count = 0;
    result = layersvt::LayerBaseTestPeer::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, kLayerName, &property_count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(property_count, 1u);

    extensions.resize(property_count);
    result = layersvt::LayerBaseTestPeer::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, kLayerName, &property_count, extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
}

TEST_F(DebugMarkerTests, LayerBaseLifecycleAndTrackerTest) {
    TEST_DESCRIPTION("Verify DebugMarker LayerBase inheritance and DeviceInstanceTracker integration");

    // Verify tracker operates correctly via GetDeviceTracker on DebugMarker::Get()
    VkPhysicalDevice mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x1234);
    VkInstance mock_instance = reinterpret_cast<VkInstance>(0x5678);

    layersvt::LayerBaseTestPeer::GetDeviceTracker(DebugMarker::Get()).SetVkInstance(mock_physical_device, mock_instance);
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetDeviceTracker(DebugMarker::Get()).GetVkInstance(mock_physical_device), mock_instance);

    layer_test::ResetLayer<DebugMarker>();
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetDeviceTracker(DebugMarker::Get()).GetVkInstance(mock_physical_device), VK_NULL_HANDLE);
}

TEST_F(DebugMarkerTests, TemplateMethodDispatchTest) {
    TEST_DESCRIPTION("Verify DebugMarker layer-specific hooks and LayerBase template method dispatching");

    // Layer-specific instance commands intercepted
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateDebugUtilsMessengerEXT"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkDestroyDebugUtilsMessengerEXT"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkSubmitDebugUtilsMessageEXT"), nullptr);

    // Common lifecycle instance commands resolved via LayerBase fallback
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateInstance"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkDestroyInstance"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumeratePhysicalDevices"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumerateInstanceExtensionProperties"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateDevice"), nullptr);

    // Layer-specific device commands intercepted
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkCmdDebugMarkerBeginEXT"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkCmdBeginDebugUtilsLabelEXT"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkSetDebugUtilsObjectNameEXT"), nullptr);

    // Common lifecycle device commands resolved via LayerBase fallback
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkCreateDevice"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkDestroyDevice"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkGetDeviceProcAddr"), nullptr);

    // Global commands resolvable with VK_NULL_HANDLE via GetInstanceProcAddr
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"), nullptr);

    // Non-global commands return nullptr when passed VK_NULL_HANDLE
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDebugUtilsMessengerEXT"), nullptr);
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCmdDebugMarkerBeginEXT"), nullptr);
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkCmdBeginDebugUtilsLabelEXT"), nullptr);
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkNonExistentCmd"), nullptr);
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkNonExistentCmd"), nullptr);

    // Non-global commands resolvable with valid instance/device handles
    VkInstance mock_instance = reinterpret_cast<VkInstance>(0x1234);
    VkDevice mock_device = reinterpret_cast<VkDevice>(0x5678);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkCreateDebugUtilsMessengerEXT"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkCmdDebugMarkerBeginEXT"), nullptr);
    EXPECT_NE(layersvt::LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkCmdBeginDebugUtilsLabelEXT"), nullptr);
}
