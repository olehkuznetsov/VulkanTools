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

#include "common/device_instance_tracker.h"
#include "common/dispatch_table_manager.h"
#include "common/layer_manifest.h"
#include "common/layer_base.h"
#include "common/log.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

using namespace layersvt;

// =============================================================================
// DeviceInstanceTracker Tests
// =============================================================================

TEST(DeviceInstanceTrackerTest, BasicTracking) {
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    auto mock_instance1 = reinterpret_cast<VkInstance>(0x1000);
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(0x2001);
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(0x2002);

    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device1), VK_NULL_HANDLE);

    tracker.SetVkInstance(mock_physical_device1, mock_instance1);
    tracker.SetVkInstance(mock_physical_device2, mock_instance1);

    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device1), mock_instance1);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device2), mock_instance1);
}

TEST(DeviceInstanceTrackerTest, RemoveInstance) {
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    auto mock_instance1 = reinterpret_cast<VkInstance>(0x1001);
    auto mock_instance2 = reinterpret_cast<VkInstance>(0x1002);
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(0x2001);
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(0x2002);

    tracker.SetVkInstance(mock_physical_device1, mock_instance1);
    tracker.SetVkInstance(mock_physical_device2, mock_instance2);

    tracker.RemoveInstance(mock_instance1);

    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device1), VK_NULL_HANDLE);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device2), mock_instance2);
}

TEST(DeviceInstanceTrackerTest, ConcurrentAccess) {
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    constexpr int kNumThreads = 8;
    constexpr int kIterations = 1000;
    std::atomic<bool> start_flag{false};
    std::vector<std::thread> threads;

    for (int thread_index = 0; thread_index < kNumThreads; ++thread_index) {
        threads.emplace_back([&, thread_index]() {
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kIterations; ++i) {
                auto instance = reinterpret_cast<VkInstance>(static_cast<uintptr_t>(0x1000 + thread_index));
                auto physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2000 + (i % 50)));
                tracker.SetVkInstance(physical_device, instance);
                auto result = tracker.GetVkInstance(physical_device);
                EXPECT_NE(result, VK_NULL_HANDLE);
            }
        });
    }

    start_flag.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
}

TEST(DeviceInstanceTrackerTest, EagerMapDevices) {
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0xABCDEF01);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(0x3001);
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(0x3002);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkEnumeratePhysicalDevices") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](VkInstance, uint32_t* property_count, VkPhysicalDevice* physical_devices) -> VkResult {
                    if (!property_count) return VK_ERROR_INITIALIZATION_FAILED;
                    if (!physical_devices) {
                        *property_count = 2;
                        return VK_SUCCESS;
                    }
                    physical_devices[0] = reinterpret_cast<VkPhysicalDevice>(0x3001);
                    physical_devices[1] = reinterpret_cast<VkPhysicalDevice>(0x3002);
                    *property_count = 2;
                    return VK_SUCCESS;
                });
        }
        return nullptr;
    };

    tracker.EagerMapDevices(mock_instance, mock_get_instance_proc_addr);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device1), mock_instance);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device2), mock_instance);
}

TEST(DeviceInstanceTrackerTest, EagerMapDevicesIncomplete) {
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0xABCDEF02);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x3003);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkEnumeratePhysicalDevices") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](VkInstance, uint32_t* property_count, VkPhysicalDevice* physical_devices) -> VkResult {
                    if (!property_count) return VK_ERROR_INITIALIZATION_FAILED;
                    if (!physical_devices) {
                        *property_count = 1;
                        return VK_INCOMPLETE;
                    }
                    physical_devices[0] = reinterpret_cast<VkPhysicalDevice>(0x3003);
                    *property_count = 1;
                    return VK_INCOMPLETE;
                });
        }
        return nullptr;
    };

    tracker.EagerMapDevices(mock_instance, mock_get_instance_proc_addr);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device), mock_instance);
}

// =============================================================================
// DispatchTableManager Tests
// =============================================================================

TEST(DispatchTableManagerTest, GetDispatchKey) {
    EXPECT_EQ(GetDispatchKey(nullptr), nullptr);

    void* mock_vtable = reinterpret_cast<void*>(0xDEADBEEF);
    void* mock_object = &mock_vtable;

    EXPECT_EQ(GetDispatchKey(mock_object), mock_vtable);
}

TEST(DispatchTableManagerTest, LoaderDataCallback) {
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();

    void* mock_device_vtable = reinterpret_cast<void*>(0x12345678);
    VkDevice mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);

    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(mock_device), nullptr);

    PFN_vkSetDeviceLoaderData dummy_callback = [](VkDevice, void*) -> VkResult { return VK_SUCCESS; };
    dispatch_table_manager.SetDeviceLoaderDataCallback(mock_device, dummy_callback);

    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(mock_device), dummy_callback);

    dispatch_table_manager.DestroyDeviceTable(GetDispatchKey(mock_device));
    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(mock_device), nullptr);
}

TEST(DispatchTableManagerTest, InstanceAndDeviceTableLifecycle) {
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0x1111);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), nullptr);
    auto* instance_table = dispatch_table_manager.InitInstanceTable(
        mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
    EXPECT_NE(instance_table, nullptr);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), instance_table);

    dispatch_table_manager.DestroyInstanceTable(GetDispatchKey(mock_instance));
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), nullptr);

    void* mock_device_vtable = reinterpret_cast<void*>(0x2222);
    auto mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);

    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(mock_device), nullptr);
    auto* device_table =
        dispatch_table_manager.InitDeviceTable(mock_device, [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; });
    EXPECT_NE(device_table, nullptr);
    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(mock_device), device_table);

    dispatch_table_manager.DestroyDeviceTable(GetDispatchKey(mock_device));
    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(mock_device), nullptr);
}

// =============================================================================
// LayerManifest Tests
// =============================================================================

TEST(LayerManifestTest, LayerProperties) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    config.description = "Sample Test Layer";
    config.spec_version = VK_API_VERSION_1_3;
    config.implementation_version = 42;

    LayerManifest manifest(config);

    uint32_t count = 0;
    VkResult result = manifest.EnumerateInstanceLayerProperties(&count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    VkLayerProperties layer_properties{};
    result = manifest.EnumerateInstanceLayerProperties(&count, &layer_properties);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(layer_properties.layerName, "VK_LAYER_TEST_Sample");
    EXPECT_STREQ(layer_properties.description, "Sample Test Layer");
    EXPECT_EQ(layer_properties.specVersion, VK_API_VERSION_1_3);
    EXPECT_EQ(layer_properties.implementationVersion, 42u);
}

TEST(LayerManifestTest, InstanceExtensions) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    config.instance_extensions = {
        {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION},
    };

    LayerManifest manifest(config);

    uint32_t count = 0;
    VkResult result = manifest.EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkExtensionProperties> extensions(count);
    result = manifest.EnumerateInstanceExtensionProperties("VK_LAYER_TEST_Sample", &count, extensions.data());
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(extensions[0].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    EXPECT_EQ(extensions[0].specVersion, static_cast<uint32_t>(VK_EXT_DEBUG_UTILS_SPEC_VERSION));

    // Querying for an unknown layer should return error
    result = manifest.EnumerateInstanceExtensionProperties("VK_LAYER_UNKNOWN", &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
}

TEST(LayerManifestTest, DeviceExtensionsDownstreamMerge) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    config.device_extensions = {
        {"VK_EXT_custom_layer_extension", 1},
    };

    LayerManifest manifest(config);

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
    VkResult result = manifest.EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, nullptr, mock_downstream);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);  // 1 from layer + 1 from driver

    std::vector<VkExtensionProperties> merged(count);
    result = manifest.EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, merged.data(), mock_downstream);
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

TEST(LayerManifestTest, ToolPropertiesMerge) {
    VkPhysicalDeviceToolPropertiesEXT layer_tool_properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
                                                               nullptr,
                                                               "CommonLayerTool",
                                                               "1.0",
                                                               VK_TOOL_PURPOSE_PROFILING_BIT_EXT,
                                                               "Diagnostic tool description",
                                                               "CommonLayer"};

    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    config.tool_properties = layer_tool_properties;

    LayerManifest manifest(config);

    // Mock downstream reporting 1 driver tool
    auto mock_downstream_tool = [](VkPhysicalDevice, uint32_t* count, VkPhysicalDeviceToolPropertiesEXT* properties) -> VkResult {
        if (!properties) {
            *count = 1;
            return VK_SUCCESS;
        }
        std::strncpy(properties[0].name, "DriverTool", VK_MAX_EXTENSION_NAME_SIZE);
        *count = 1;
        return VK_SUCCESS;
    };

    uint32_t count = 0;
    VkResult result = manifest.GetPhysicalDeviceToolProperties(nullptr, &count, nullptr, mock_downstream_tool);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 2u);

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools(count);
    result = manifest.GetPhysicalDeviceToolProperties(nullptr, &count, tools.data(), mock_downstream_tool);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_STREQ(tools[0].name, "DriverTool");
    EXPECT_STREQ(tools[1].name, "CommonLayerTool");
}

TEST(LayerManifestTest, DeviceLayerPropertiesOverload) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    LayerManifest manifest(config);

    uint32_t count = 0;
    VkResult result = manifest.EnumerateDeviceLayerProperties(reinterpret_cast<VkPhysicalDevice>(0x123), &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1u);
}

TEST(LayerManifestTest, DeviceExtensionsNullDownstream) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    config.device_extensions = {
        {"VK_EXT_standalone_extension", 1},
    };
    LayerManifest manifest(config);

    uint32_t count = 0;
    EXPECT_EQ(manifest.EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, nullptr, nullptr), VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkExtensionProperties> extensions(count);
    EXPECT_EQ(manifest.EnumerateDeviceExtensionProperties(nullptr, nullptr, &count, extensions.data(), nullptr), VK_SUCCESS);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(extensions[0].extensionName, "VK_EXT_standalone_extension");
}

TEST(LayerManifestTest, QueryDifferentLayerName) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    LayerManifest manifest(config);

    uint32_t count = 5;
    EXPECT_EQ(manifest.EnumerateInstanceExtensionProperties("VK_LAYER_OTHER", &count, nullptr), VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(count, 0u);

    count = 5;
    EXPECT_EQ(manifest.EnumerateDeviceExtensionProperties(nullptr, "VK_LAYER_OTHER", &count, nullptr, nullptr),
              VK_ERROR_LAYER_NOT_PRESENT);
    EXPECT_EQ(count, 0u);
}

TEST(LayerManifestTest, ToolPropertiesErrorPropagation) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    LayerManifest manifest(config);

    auto error_downstream_tool = [](VkPhysicalDevice, uint32_t*, VkPhysicalDeviceToolPropertiesEXT*) -> VkResult {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    };

    uint32_t count = 0;
    EXPECT_EQ(manifest.GetPhysicalDeviceToolProperties(nullptr, &count, nullptr, error_downstream_tool),
              VK_ERROR_OUT_OF_HOST_MEMORY);
}

TEST(LayerManifestTest, EnumerateInstanceExtensionsNullCount) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    LayerManifest manifest(config);

    EXPECT_EQ(manifest.EnumerateInstanceExtensionProperties("VK_LAYER_UNKNOWN", nullptr, nullptr), VK_ERROR_INITIALIZATION_FAILED);
}

// =============================================================================
// LayerBase Mock Tests
// =============================================================================

class MockTestLayer : public LayerBase {
   public:
    bool pre_create_instance_called = false;
    bool post_create_instance_called = false;
    bool pre_destroy_instance_called = false;
    bool post_destroy_instance_called = false;

    bool pre_create_device_called = false;
    bool on_device_created_called = false;
    bool pre_destroy_device_called = false;
    bool post_destroy_device_called = false;

    void PreCreateInstance(VkInstanceCreateInfo*, const VkAllocationCallbacks*) override { pre_create_instance_called = true; }
    void PostCreateInstance(VkInstance, const VkInstanceCreateInfo*) override { post_create_instance_called = true; }
    void PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) override { pre_destroy_instance_called = true; }
    void PostDestroyInstance(VkInstance) override { post_destroy_instance_called = true; }

    void PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo*, const VkAllocationCallbacks*) override {
        pre_create_device_called = true;
    }
    void OnDeviceCreated(VkDevice, VkPhysicalDevice, const VkDeviceCreateInfo*) override { on_device_created_called = true; }
    void PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) override { pre_destroy_device_called = true; }
    void PostDestroyDevice(VkDevice) override { post_destroy_device_called = true; }
};

TEST(LayerBaseTest, HookInvocations) {
    MockTestLayer layer;
    EXPECT_FALSE(layer.pre_create_instance_called);
    EXPECT_FALSE(layer.pre_create_device_called);
    EXPECT_FALSE(layer.on_device_created_called);

    // Verify hooks trigger as expected
    layer.PreCreateInstance(nullptr, nullptr);
    EXPECT_TRUE(layer.pre_create_instance_called);

    layer.PreCreateDevice(VK_NULL_HANDLE, nullptr, nullptr);
    EXPECT_TRUE(layer.pre_create_device_called);

    layer.OnDeviceCreated(VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    EXPECT_TRUE(layer.on_device_created_called);
}

TEST(LayerBaseTest, CreateInstanceWithMockChain) {
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    static void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    static auto mock_instance_handle = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkCreateInstance") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance_handle) -> VkResult {
                    *instance_handle = mock_instance_handle;
                    return VK_SUCCESS;
                });
        }
        if (std::strcmp(function_name, "vkEnumeratePhysicalDevices") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](VkInstance, uint32_t* property_count, VkPhysicalDevice* physical_devices) -> VkResult {
                    if (!physical_devices) {
                        *property_count = 1;
                        return VK_SUCCESS;
                    }
                    physical_devices[0] = reinterpret_cast<VkPhysicalDevice>(0x9999);
                    *property_count = 1;
                    return VK_SUCCESS;
                });
        }
        if (std::strcmp(function_name, "vkDestroyInstance") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkInstance, const VkAllocationCallbacks*) {});
        }
        return nullptr;
    };

    VkLayerInstanceLink layer_link{nullptr, mock_get_instance_proc_addr, nullptr};
    VkLayerInstanceCreateInfo chain_info{VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, nullptr, VK_LAYER_LINK_INFO, {&layer_link}};

    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pNext = &chain_info;
    MockTestLayer layer;
    VkInstance instance = VK_NULL_HANDLE;

    EXPECT_EQ(layer.CreateInstance(&instance_create_info, nullptr, &instance), VK_SUCCESS);
    EXPECT_EQ(instance, mock_instance_handle);
    EXPECT_TRUE(layer.pre_create_instance_called);
    EXPECT_TRUE(layer.post_create_instance_called);
    EXPECT_NE(dispatch_table_manager.GetInstanceDispatchTable(instance), nullptr);
    // Verify eager mapping mapped physical device 0x9999 to instance
    EXPECT_EQ(tracker.GetVkInstance(reinterpret_cast<VkPhysicalDevice>(0x9999)), instance);

    layer.DestroyInstance(instance, nullptr);
    EXPECT_TRUE(layer.pre_destroy_instance_called);
    EXPECT_TRUE(layer.post_destroy_instance_called);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(instance), nullptr);
    EXPECT_EQ(tracker.GetVkInstance(reinterpret_cast<VkPhysicalDevice>(0x9999)), VK_NULL_HANDLE);
}

TEST(LayerBaseTest, CreateInstanceNullHandling) {
    LayerBase layer;
    VkInstance instance = VK_NULL_HANDLE;
    // Null create info
    EXPECT_EQ(layer.CreateInstance(nullptr, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);

    // Missing chain info
    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    EXPECT_EQ(layer.CreateInstance(&instance_create_info, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, PreCreateNotInvokedOnMissingChain) {
    MockTestLayer layer;
    VkInstance instance = VK_NULL_HANDLE;
    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    EXPECT_EQ(layer.CreateInstance(&instance_create_info, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_FALSE(layer.pre_create_instance_called);
}

TEST(LayerBaseTest, NullHandleDestroySafety) {
    MockTestLayer layer;
    layer.DestroyInstance(VK_NULL_HANDLE, nullptr);
    EXPECT_FALSE(layer.pre_destroy_instance_called);
    EXPECT_FALSE(layer.post_destroy_instance_called);

    layer.DestroyDevice(VK_NULL_HANDLE, nullptr);
    EXPECT_FALSE(layer.pre_destroy_device_called);
    EXPECT_FALSE(layer.post_destroy_device_called);
}

class SubclassWithInspection : public LayerBase {
   public:
    bool inspected = false;
    void PreCreateInstance(VkInstanceCreateInfo* create_info, const VkAllocationCallbacks*) override {
        if (create_info && create_info->pApplicationInfo) {
            inspected = true;
        }
    }
};

TEST(LayerBaseTest, CreateInstanceNullSafetyInHook) {
    SubclassWithInspection layer;
    VkInstance instance = VK_NULL_HANDLE;
    EXPECT_EQ(layer.CreateInstance(nullptr, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_FALSE(layer.inspected);

    VkInstanceCreateInfo instance_create_info{};
    EXPECT_EQ(layer.CreateInstance(&instance_create_info, nullptr, nullptr), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, PreCreateInstanceMutation) {
    class MutatingLayer : public LayerBase {
       public:
        void PreCreateInstance(VkInstanceCreateInfo* create_info, const VkAllocationCallbacks*) override {
            if (create_info) {
                create_info->flags = 0xABCD;
            }
        }
    };

    static VkInstanceCreateFlags received_flags = 0;
    static void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    static auto mock_instance_handle = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkCreateInstance") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks*, VkInstance* instance) -> VkResult {
                    received_flags = create_info->flags;
                    *instance = mock_instance_handle;
                    return VK_SUCCESS;
                });
        }
        return nullptr;
    };

    VkLayerInstanceLink layer_link{nullptr, mock_get_instance_proc_addr, nullptr};
    VkLayerInstanceCreateInfo chain_info{VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, nullptr, VK_LAYER_LINK_INFO, {&layer_link}};

    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pNext = &chain_info;

    MutatingLayer layer;
    VkInstance instance = VK_NULL_HANDLE;
    EXPECT_EQ(layer.CreateInstance(&instance_create_info, nullptr, &instance), VK_SUCCESS);
    EXPECT_EQ(received_flags, 0xABCDu);
}

TEST(LayerBaseTest, TeardownOrdering) {
    class TeardownOrderLayer : public LayerBase {
       public:
        VkInstance captured_instance_in_post_destroy = VK_NULL_HANDLE;
        VkPhysicalDevice mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x9999);

        void PostDestroyInstance(VkInstance) override {
            captured_instance_in_post_destroy = DeviceInstanceTracker::Get().GetVkInstance(mock_physical_device);
        }
    };

    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x9999);

    dispatch_table_manager.InitInstanceTable(mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkDestroyInstance") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkInstance, const VkAllocationCallbacks*) {});
        }
        return nullptr;
    });
    tracker.SetVkInstance(mock_physical_device, mock_instance);

    TeardownOrderLayer layer;
    layer.DestroyInstance(mock_instance, nullptr);

    // Verify PostDestroyInstance could still query the physical device mapping
    EXPECT_EQ(layer.captured_instance_in_post_destroy, mock_instance);
    // After DestroyInstance finishes, mapping is cleaned up
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device), VK_NULL_HANDLE);
}

TEST(LayerBaseTest, CreateDeviceWithMockChain) {
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x5555);
    tracker.SetVkInstance(mock_physical_device, mock_instance);

    static void* mock_device_vtable = reinterpret_cast<void*>(0x55667788);
    static auto mock_device_handle = reinterpret_cast<VkDevice>(&mock_device_vtable);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkCreateDevice") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkPhysicalDevice, const VkDeviceCreateInfo*,
                                                            const VkAllocationCallbacks*, VkDevice* device_handle) -> VkResult {
                *device_handle = mock_device_handle;
                return VK_SUCCESS;
            });
        }
        return nullptr;
    };

    PFN_vkGetDeviceProcAddr mock_get_device_proc_addr = [](VkDevice, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkDestroyDevice") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkDevice, const VkAllocationCallbacks*) {});
        }
        return nullptr;
    };

    VkLayerDeviceLink layer_link{nullptr, mock_get_instance_proc_addr, mock_get_device_proc_addr};
    VkLayerDeviceCreateInfo chain_info{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, nullptr, VK_LAYER_LINK_INFO, {&layer_link}};

    PFN_vkSetDeviceLoaderData mock_loader_callback = [](VkDevice, void*) -> VkResult { return VK_SUCCESS; };
    VkLayerDeviceCreateInfo callback_info{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, &chain_info, VK_LOADER_DATA_CALLBACK, {}};
    callback_info.u.pfnSetDeviceLoaderData = mock_loader_callback;

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &callback_info;

    MockTestLayer layer;
    VkDevice device = VK_NULL_HANDLE;

    EXPECT_EQ(layer.CreateDevice(mock_physical_device, &device_create_info, nullptr, &device), VK_SUCCESS);
    EXPECT_EQ(device, mock_device_handle);
    EXPECT_TRUE(layer.pre_create_device_called);
    EXPECT_TRUE(layer.on_device_created_called);
    EXPECT_NE(dispatch_table_manager.GetDeviceDispatchTable(device), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(device), mock_loader_callback);

    layer.DestroyDevice(device, nullptr);
    EXPECT_TRUE(layer.pre_destroy_device_called);
    EXPECT_TRUE(layer.post_destroy_device_called);
    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(device), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(device), nullptr);
}

TEST(LayerBaseTest, CreateDeviceNullHandling) {
    LayerBase layer;
    VkDevice device = VK_NULL_HANDLE;
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x5555);

    // Null create info
    EXPECT_EQ(layer.CreateDevice(mock_physical_device, nullptr, nullptr, &device), VK_ERROR_INITIALIZATION_FAILED);

    // Missing chain info
    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    EXPECT_EQ(layer.CreateDevice(mock_physical_device, &device_create_info, nullptr, &device), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, CreateDeviceInvalidInputSafety) {
    LayerBase layer;
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    auto mock_untracked_physical_device = reinterpret_cast<VkPhysicalDevice>(0xBAADF00D);
    // Untracked physical device must fail cleanly without crashing
    EXPECT_EQ(layer.CreateDevice(mock_untracked_physical_device, &create_info, nullptr, &device), VK_ERROR_INITIALIZATION_FAILED);

    // Null device pointer must fail cleanly
    EXPECT_EQ(layer.CreateDevice(mock_untracked_physical_device, &create_info, nullptr, nullptr), VK_ERROR_INITIALIZATION_FAILED);

    // VK_NULL_HANDLE physical device must fail cleanly
    EXPECT_EQ(layer.CreateDevice(VK_NULL_HANDLE, &create_info, nullptr, &device), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, EnumeratePhysicalDevicesMapping) {
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x7777);

    dispatch_table_manager.InitInstanceTable(mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkEnumeratePhysicalDevices") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](VkInstance, uint32_t* property_count, VkPhysicalDevice* physical_devices) -> VkResult {
                    if (!property_count) return VK_ERROR_INITIALIZATION_FAILED;
                    if (!physical_devices) {
                        *property_count = 1;
                        return VK_SUCCESS;
                    }
                    physical_devices[0] = reinterpret_cast<VkPhysicalDevice>(0x7777);
                    *property_count = 1;
                    return VK_SUCCESS;
                });
        }
        return nullptr;
    });

    LayerBase layer;
    uint32_t count = 0;
    EXPECT_EQ(layer.EnumeratePhysicalDevices(mock_instance, &count, nullptr), VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkPhysicalDevice> devices(count);
    EXPECT_EQ(layer.EnumeratePhysicalDevices(mock_instance, &count, devices.data()), VK_SUCCESS);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device), mock_instance);
}

TEST(LayerBaseTest, DefaultHooksExecution) {
    LayerManifest::Config config;
    config.layer_name = "VK_LAYER_TEST_Sample";
    LayerManifest manifest(config);
    LayerBase base(&manifest);
    EXPECT_EQ(base.GetManifest(), &manifest);

    // Execute default no-op hooks to verify base class behavior
    VkInstanceCreateInfo instance_create_info{};
    base.PreCreateInstance(&instance_create_info, nullptr);
    base.PostCreateInstance(VK_NULL_HANDLE, &instance_create_info);
    base.PreDestroyInstance(VK_NULL_HANDLE, nullptr);
    base.PostDestroyInstance(VK_NULL_HANDLE);

    VkDeviceCreateInfo device_create_info{};
    base.PreCreateDevice(VK_NULL_HANDLE, &device_create_info, nullptr);
    base.OnDeviceCreated(VK_NULL_HANDLE, VK_NULL_HANDLE, &device_create_info);
    base.PreDestroyDevice(VK_NULL_HANDLE, nullptr);
    base.PostDestroyDevice(VK_NULL_HANDLE);
}

TEST(LayerBaseTest, EnumeratePhysicalDeviceGroupsMapping) {
    auto& dispatch_table_manager = DispatchTableManager::Get();
    dispatch_table_manager.Clear();
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();

    void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(0x8881);
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(0x8882);

    dispatch_table_manager.InitInstanceTable(mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkEnumeratePhysicalDeviceGroups") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](VkInstance, uint32_t* property_count, VkPhysicalDeviceGroupProperties* physical_device_groups) -> VkResult {
                    if (!property_count) return VK_ERROR_INITIALIZATION_FAILED;
                    if (!physical_device_groups) {
                        *property_count = 1;
                        return VK_SUCCESS;
                    }
                    physical_device_groups[0].physicalDeviceCount = 2;
                    physical_device_groups[0].physicalDevices[0] = reinterpret_cast<VkPhysicalDevice>(0x8881);
                    physical_device_groups[0].physicalDevices[1] = reinterpret_cast<VkPhysicalDevice>(0x8882);
                    *property_count = 1;
                    return VK_SUCCESS;
                });
        }
        return nullptr;
    });

    LayerBase layer;
    void* missing_instance_vtable = reinterpret_cast<void*>(0xBAADF00D);
    auto missing_instance = reinterpret_cast<VkInstance>(&missing_instance_vtable);
    // Missing table or function
    EXPECT_EQ(layer.EnumeratePhysicalDeviceGroups(missing_instance, nullptr, nullptr), VK_ERROR_INITIALIZATION_FAILED);

    uint32_t count = 0;
    EXPECT_EQ(layer.EnumeratePhysicalDeviceGroups(mock_instance, &count, nullptr), VK_SUCCESS);
    EXPECT_EQ(count, 1u);

    std::vector<VkPhysicalDeviceGroupProperties> groups(count);
    EXPECT_EQ(layer.EnumeratePhysicalDeviceGroups(mock_instance, &count, groups.data()), VK_SUCCESS);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device1), mock_instance);
    EXPECT_EQ(tracker.GetVkInstance(mock_physical_device2), mock_instance);
}

TEST(LayerBaseTest, EnumeratePhysicalDevicesNullTable) {
    LayerBase layer;
    void* missing_instance_vtable = reinterpret_cast<void*>(0xBAADF00D);
    auto missing_instance = reinterpret_cast<VkInstance>(&missing_instance_vtable);
    uint32_t count = 0;
    EXPECT_EQ(layer.EnumeratePhysicalDevices(missing_instance, &count, nullptr), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, CreateInstanceNullFpCreateInstance) {
    static PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char*) -> PFN_vkVoidFunction {
        return nullptr;
    };
    VkLayerInstanceLink layer_link{nullptr, mock_get_instance_proc_addr, nullptr};
    VkLayerInstanceCreateInfo chain_info{VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, nullptr, VK_LAYER_LINK_INFO, {&layer_link}};

    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pNext = &chain_info;

    LayerBase layer;
    VkInstance instance = VK_NULL_HANDLE;
    EXPECT_EQ(layer.CreateInstance(&instance_create_info, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, CreateDeviceNullFpCreateDevice) {
    auto& tracker = DeviceInstanceTracker::Get();
    tracker.Clear();
    void* mock_instance_vtable = reinterpret_cast<void*>(0x11223344);
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(0x5555);
    tracker.SetVkInstance(mock_physical_device, mock_instance);

    static PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char*) -> PFN_vkVoidFunction {
        return nullptr;
    };
    static PFN_vkGetDeviceProcAddr mock_get_device_proc_addr = [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; };
    VkLayerDeviceLink layer_link{nullptr, mock_get_instance_proc_addr, mock_get_device_proc_addr};
    VkLayerDeviceCreateInfo chain_info{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, nullptr, VK_LAYER_LINK_INFO, {&layer_link}};

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &chain_info;

    LayerBase layer;
    VkDevice device = VK_NULL_HANDLE;
    EXPECT_EQ(layer.CreateDevice(mock_physical_device, &device_create_info, nullptr, &device), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LogTest, MacroCompilation) {
    VT_LOGI("CommonTest", "Info message test: %d", 123);
    VT_LOGW("CommonTest", "Warning message test: %s", "warn");
    VT_LOGE("CommonTest", "Error message test: %f", 3.14);
}
