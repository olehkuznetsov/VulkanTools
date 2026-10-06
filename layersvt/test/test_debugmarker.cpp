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
#include <cstring>

#include "test/common/layer_base_test_peer.h"

static const char* kLayerName = "VK_LAYER_GOOGLE_DebugMarker";

class DebugMarkerTests : public VkTestFramework {
   public:
    ~DebugMarkerTests() {};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {};

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
    result = layersvt::LayerBaseTestPeer::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, kLayerName, &property_count,
                                                                             extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
}

TEST_F(DebugMarkerTests, LayerBaseLifecycleAndTrackerTest) {
    TEST_DESCRIPTION("Verify DebugMarker LayerBase inheritance and DispatchTableManager integration");

    // A physical device shares the dispatch key of its parent instance.
    void* mock_dispatch_key = reinterpret_cast<void*>(0x1000);
    void* mock_instance_object = mock_dispatch_key;
    void* mock_physical_device_object = mock_dispatch_key;
    VkInstance mock_instance = reinterpret_cast<VkInstance>(&mock_instance_object);
    VkPhysicalDevice mock_physical_device = reinterpret_cast<VkPhysicalDevice>(&mock_physical_device_object);

    layersvt::LayerBaseTestPeer::GetDispatchTableManager(DebugMarker::Get())
        .InitInstanceTable(mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetVkInstance(mock_physical_device), mock_instance);

    layer_test::ResetLayer<DebugMarker>();
    EXPECT_EQ(layersvt::LayerBaseTestPeer::GetVkInstance(mock_physical_device), VK_NULL_HANDLE);
}

TEST_F(DebugMarkerTests, PreDestroyDeviceCleanupTest) {
    TEST_DESCRIPTION("Verify that DestroyDevice cleans up tracked objects associated with that device");

    layer_test::ResetLayer<DebugMarker>();

    void* mock_dev1_vtable = reinterpret_cast<void*>(0x1000);
    VkDevice dev1 = reinterpret_cast<VkDevice>(&mock_dev1_vtable);
    void* mock_dev2_vtable = reinterpret_cast<void*>(0x2000);
    VkDevice dev2 = reinterpret_cast<VkDevice>(&mock_dev2_vtable);

    layersvt::LayerBaseTestPeer::GetDispatchTableManager(DebugMarker::Get())
        .InitDeviceTable(dev1, [](VkDevice, const char*) -> PFN_vkVoidFunction {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkDevice, const VkAllocationCallbacks*) {});
        });
    layersvt::LayerBaseTestPeer::GetDispatchTableManager(DebugMarker::Get())
        .InitDeviceTable(dev2, [](VkDevice, const char*) -> PFN_vkVoidFunction {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkDevice, const VkAllocationCallbacks*) {});
        });

    DebugMarker::Get().SetDebugObjectName((uint64_t)dev1, VK_OBJECT_TYPE_BUFFER, 0x1111, "Buffer1");
    DebugMarker::Get().SetDebugObjectName((uint64_t)dev2, VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2");

    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x1111, "Buffer1"));
    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2"));

    // Destroy dev1 - should remove Buffer1 but keep Buffer2
    layersvt::LayerBaseTestPeer::DestroyDevice(dev1, nullptr);

    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x1111, "Buffer1"));
    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2"));

    // Destroy dev2 - should remove Buffer2
    layersvt::LayerBaseTestPeer::DestroyDevice(dev2, nullptr);
    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x2222, "Buffer2"));
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

TEST_F(DebugMarkerTests, MissingDownstreamExtensionTest) {
    TEST_DESCRIPTION("Verify debug marker and debug utils hooks do not crash when the downstream lacks the extensions");

    // Dispatchable handles of a device share the device's dispatch key.
    void* mock_dispatch_key = reinterpret_cast<void*>(0x3000);
    void* mock_device_object = mock_dispatch_key;
    void* mock_command_buffer_object = mock_dispatch_key;
    VkDevice mock_device = reinterpret_cast<VkDevice>(&mock_device_object);
    VkCommandBuffer mock_command_buffer = reinterpret_cast<VkCommandBuffer>(&mock_command_buffer_object);

    // The downstream device exposes no commands at all.
    layersvt::LayerBaseTestPeer::GetDispatchTableManager(DebugMarker::Get())
        .InitDeviceTable(mock_device, [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; });

    auto cmd_begin = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(
        layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkCmdDebugMarkerBeginEXT"));
    auto cmd_end =
        reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkCmdDebugMarkerEndEXT"));
    auto set_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        layersvt::LayerBaseTestPeer::GetKnownDeviceCommand("vkSetDebugUtilsObjectNameEXT"));
    ASSERT_NE(cmd_begin, nullptr);
    ASSERT_NE(cmd_end, nullptr);
    ASSERT_NE(set_object_name, nullptr);

    VkDebugMarkerMarkerInfoEXT marker_info{VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT};
    marker_info.pMarkerName = "Marker";
    cmd_begin(mock_command_buffer, &marker_info);
    cmd_end(mock_command_buffer);

    VkDebugUtilsObjectNameInfoEXT name_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    name_info.objectType = VK_OBJECT_TYPE_BUFFER;
    name_info.objectHandle = 0x4444;
    name_info.pObjectName = "Buffer";
    EXPECT_EQ(set_object_name(mock_device, &name_info), VK_SUCCESS);
    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0x4444, "Buffer"));
}

namespace {

struct MessengerCallbackLog {
    uint32_t call_count = 0;
    VkDebugUtilsMessageSeverityFlagBitsEXT last_severity = static_cast<VkDebugUtilsMessageSeverityFlagBitsEXT>(0);
};

VKAPI_ATTR VkBool32 VKAPI_CALL LogMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*,
                                                    void* pUserData) {
    auto* log = static_cast<MessengerCallbackLog*>(pUserData);
    ++log->call_count;
    log->last_severity = messageSeverity;
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeMessengerCreateInfo(VkDebugUtilsMessageSeverityFlagsEXT severity,
                                                           VkDebugUtilsMessageTypeFlagsEXT type, MessengerCallbackLog* log) {
    VkDebugUtilsMessengerCreateInfoEXT create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    create_info.messageSeverity = severity;
    create_info.messageType = type;
    create_info.pfnUserCallback = LogMessengerCallback;
    create_info.pUserData = log;
    return create_info;
}

struct DebugUtilsMessengerCommands {
    PFN_vkCreateDebugUtilsMessengerEXT create;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy;
    PFN_vkSubmitDebugUtilsMessageEXT submit;
};

DebugUtilsMessengerCommands GetDebugUtilsMessengerCommands() {
    return {reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateDebugUtilsMessengerEXT")),
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkDestroyDebugUtilsMessengerEXT")),
            reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(
                layersvt::LayerBaseTestPeer::GetKnownInstanceCommand("vkSubmitDebugUtilsMessageEXT"))};
}

}  // namespace

TEST_F(DebugMarkerTests, EmulatedDebugUtilsMessengerTest) {
    TEST_DESCRIPTION("Verify debug utils messengers are emulated when the downstream lacks VK_EXT_debug_utils");

    void* mock_instance_object = reinterpret_cast<void*>(0x5000);
    VkInstance mock_instance = reinterpret_cast<VkInstance>(&mock_instance_object);

    // The downstream instance only exposes vkDestroyInstance.
    layersvt::LayerBaseTestPeer::GetDispatchTableManager(DebugMarker::Get())
        .InitInstanceTable(mock_instance, [](VkInstance, const char* name) -> PFN_vkVoidFunction {
            if (strcmp(name, "vkDestroyInstance") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(+[](VkInstance, const VkAllocationCallbacks*) {});
            }
            return nullptr;
        });

    DebugUtilsMessengerCommands commands = GetDebugUtilsMessengerCommands();
    ASSERT_NE(commands.create, nullptr);
    ASSERT_NE(commands.destroy, nullptr);
    ASSERT_NE(commands.submit, nullptr);

    MessengerCallbackLog error_log;
    MessengerCallbackLog verbose_log;
    VkDebugUtilsMessengerCreateInfoEXT error_info = MakeMessengerCreateInfo(
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &error_log);
    VkDebugUtilsMessengerCreateInfoEXT verbose_info = MakeMessengerCreateInfo(
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &verbose_log);

    VkDebugUtilsMessengerEXT error_messenger = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT verbose_messenger = VK_NULL_HANDLE;
    EXPECT_EQ(commands.create(mock_instance, &error_info, nullptr, &error_messenger), VK_SUCCESS);
    EXPECT_EQ(commands.create(mock_instance, &verbose_info, nullptr, &verbose_messenger), VK_SUCCESS);
    EXPECT_NE(error_messenger, VK_NULL_HANDLE);
    EXPECT_NE(verbose_messenger, VK_NULL_HANDLE);
    EXPECT_NE(error_messenger, verbose_messenger);

    // Only the messenger whose severity and type masks match is invoked.
    VkDebugUtilsMessengerCallbackDataEXT callback_data{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
    callback_data.pMessage = "message";
    commands.submit(mock_instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                    &callback_data);
    EXPECT_EQ(error_log.call_count, 1u);
    EXPECT_EQ(error_log.last_severity, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    EXPECT_EQ(verbose_log.call_count, 0u);

    // Matching severity with a non-matching type does not invoke the messenger.
    commands.submit(mock_instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
                    &callback_data);
    EXPECT_EQ(error_log.call_count, 1u);
    EXPECT_EQ(verbose_log.call_count, 0u);

    // A destroyed messenger is no longer invoked; destroying VK_NULL_HANDLE is a no-op.
    commands.destroy(mock_instance, error_messenger, nullptr);
    commands.destroy(mock_instance, VK_NULL_HANDLE, nullptr);
    commands.submit(mock_instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                    &callback_data);
    EXPECT_EQ(error_log.call_count, 1u);

    commands.submit(mock_instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
                    &callback_data);
    EXPECT_EQ(verbose_log.call_count, 1u);

    // Destroying the instance removes its remaining emulated messengers.
    layersvt::LayerBaseTestPeer::DestroyInstance(mock_instance, nullptr);
    commands.submit(mock_instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
                    &callback_data);
    EXPECT_EQ(verbose_log.call_count, 1u);
}

namespace {

uint32_t downstream_create_messenger_calls = 0;
uint32_t downstream_destroy_messenger_calls = 0;
uint32_t downstream_submit_message_calls = 0;
const VkDebugUtilsMessengerEXT kDownstreamMessenger = (VkDebugUtilsMessengerEXT)0xABCDu;

}  // namespace

TEST_F(DebugMarkerTests, ForwardedDebugUtilsMessengerTest) {
    TEST_DESCRIPTION("Verify debug utils messenger commands are forwarded when the downstream supports VK_EXT_debug_utils");

    downstream_create_messenger_calls = 0;
    downstream_destroy_messenger_calls = 0;
    downstream_submit_message_calls = 0;

    void* mock_instance_object = reinterpret_cast<void*>(0x6000);
    VkInstance mock_instance = reinterpret_cast<VkInstance>(&mock_instance_object);

    layersvt::LayerBaseTestPeer::GetDispatchTableManager(DebugMarker::Get())
        .InitInstanceTable(mock_instance, [](VkInstance, const char* name) -> PFN_vkVoidFunction {
            if (strcmp(name, "vkCreateDebugUtilsMessengerEXT") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(+[](VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*,
                                                                const VkAllocationCallbacks*,
                                                                VkDebugUtilsMessengerEXT* pMessenger) {
                    ++downstream_create_messenger_calls;
                    *pMessenger = kDownstreamMessenger;
                    return VK_SUCCESS;
                });
            }
            if (strcmp(name, "vkDestroyDebugUtilsMessengerEXT") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(
                    +[](VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks*) {
                        ++downstream_destroy_messenger_calls;
                    });
            }
            if (strcmp(name, "vkSubmitDebugUtilsMessageEXT") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(
                    +[](VkInstance, VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                        const VkDebugUtilsMessengerCallbackDataEXT*) { ++downstream_submit_message_calls; });
            }
            return nullptr;
        });

    DebugUtilsMessengerCommands commands = GetDebugUtilsMessengerCommands();
    MessengerCallbackLog log;
    VkDebugUtilsMessengerCreateInfoEXT create_info = MakeMessengerCreateInfo(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                                                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &log);

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    EXPECT_EQ(commands.create(mock_instance, &create_info, nullptr, &messenger), VK_SUCCESS);
    EXPECT_EQ(messenger, kDownstreamMessenger);
    EXPECT_EQ(downstream_create_messenger_calls, 1u);

    // Messages go to the downstream; the layer does not invoke the callback itself.
    VkDebugUtilsMessengerCallbackDataEXT callback_data{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
    commands.submit(mock_instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                    &callback_data);
    EXPECT_EQ(downstream_submit_message_calls, 1u);
    EXPECT_EQ(log.call_count, 0u);

    commands.destroy(mock_instance, messenger, nullptr);
    EXPECT_EQ(downstream_destroy_messenger_calls, 1u);
}
