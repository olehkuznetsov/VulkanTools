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

#include "common/dispatch_downstream.h"
#include "common/layer_base.h"
#include "layer_base_test_peer.h"
#include <gtest/gtest.h>

using namespace layersvt;

TEST(DispatchDownstreamTest, DispatchDownstream) {
    LayerBase layer;
    auto& dispatch_table_manager = LayerBaseTestPeer::GetDispatchTableManager(layer);

    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x2222));
    auto mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);

    // 1. Unregistered handles (no dispatch table present)
    // DispatchDownstreamOrSuccess returns VK_SUCCESS fallback for VkResult commands
    uint32_t count = 0;
    VkResult instance_result =
        DispatchDownstreamOrSuccess<&VkuInstanceDispatchTable::EnumeratePhysicalDevices>(mock_instance, &count, nullptr);
    EXPECT_EQ(instance_result, VK_SUCCESS);

    VkResult device_result = DispatchDownstreamOrSuccess<&VkuDeviceDispatchTable::DeviceWaitIdle>(mock_device);
    EXPECT_EQ(device_result, VK_SUCCESS);

    // void return type safely no-ops with DispatchDownstreamOr
    DispatchDownstreamOr<&VkuInstanceDispatchTable::DestroyInstance>([] {}, mock_instance, nullptr);
    DispatchDownstreamOr<&VkuDeviceDispatchTable::DestroyDevice>([] {}, mock_device, nullptr);

    // DispatchDownstreamOr returns custom fallback value or lambda
    EXPECT_EQ((DispatchDownstreamOr<&VkuDeviceDispatchTable::DeviceWaitIdle>(VK_TIMEOUT, mock_device)), VK_TIMEOUT);
    EXPECT_EQ((DispatchDownstreamOr<&VkuDeviceDispatchTable::DeviceWaitIdle>([] { return VK_NOT_READY; }, mock_device)),
              VK_NOT_READY);

    // 2. Initialized tables with null function pointers (fallback behavior)
    dispatch_table_manager.InitInstanceTable(mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
    dispatch_table_manager.InitDeviceTable(mock_device, [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; });

    EXPECT_EQ((DispatchDownstreamOrSuccess<&VkuInstanceDispatchTable::EnumeratePhysicalDevices>(mock_instance, &count, nullptr)),
              VK_SUCCESS);
    EXPECT_EQ((DispatchDownstreamOrSuccess<&VkuDeviceDispatchTable::DeviceWaitIdle>(mock_device)), VK_SUCCESS);
    DispatchDownstreamOr<&VkuInstanceDispatchTable::DestroyInstance>([] {}, mock_instance, nullptr);
    DispatchDownstreamOr<&VkuDeviceDispatchTable::DestroyDevice>([] {}, mock_device, nullptr);

    EXPECT_EQ((DispatchDownstreamOr<&VkuDeviceDispatchTable::DeviceWaitIdle>(VK_TIMEOUT, mock_device)), VK_TIMEOUT);

    // 3. Initialized tables with valid mock function pointers (downstream forwarding)
    static bool instance_function_called = false;
    static bool device_function_called = false;
    instance_function_called = false;
    device_function_called = false;

    auto* instance_table = dispatch_table_manager.GetInstanceDispatchTable(mock_instance);
    ASSERT_NE(instance_table, nullptr);
    instance_table->EnumeratePhysicalDevices = [](VkInstance, uint32_t* physical_device_count, VkPhysicalDevice*) -> VkResult {
        instance_function_called = true;
        if (physical_device_count) *physical_device_count = 42;
        return VK_INCOMPLETE;
    };

    count = 0;
    EXPECT_EQ((DispatchDownstream<&VkuInstanceDispatchTable::EnumeratePhysicalDevices>(mock_instance, &count, nullptr)),
              VK_INCOMPLETE);
    EXPECT_TRUE(instance_function_called);
    EXPECT_EQ(count, 42u);

    auto* device_table = dispatch_table_manager.GetDeviceDispatchTable(mock_device);
    ASSERT_NE(device_table, nullptr);
    device_table->DeviceWaitIdle = [](VkDevice) -> VkResult {
        device_function_called = true;
        return VK_NOT_READY;
    };

    EXPECT_EQ((DispatchDownstream<&VkuDeviceDispatchTable::DeviceWaitIdle>(mock_device)), VK_NOT_READY);
    EXPECT_TRUE(device_function_called);

    // 4. Physical device dispatch and fallback behavior
    void* mock_physical_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3333));
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(&mock_physical_device_vtable);

    // Unmapped physical device must return fallback rather than crashing
    VkPhysicalDeviceProperties properties{};
    DispatchDownstreamOr<&VkuInstanceDispatchTable::GetPhysicalDeviceProperties>(
        [] {}, mock_physical_device, &properties);

    // Mapped physical device forwards downstream through instance table
    dispatch_table_manager.SetVkInstance(mock_physical_device, mock_instance);

    static bool physical_device_function_called = false;
    physical_device_function_called = false;
    instance_table->GetPhysicalDeviceProperties = [](VkPhysicalDevice, VkPhysicalDeviceProperties* physical_device_properties) {
        physical_device_function_called = true;
        if (physical_device_properties) physical_device_properties->apiVersion = VK_API_VERSION_1_3;
    };

    DispatchDownstream<&VkuInstanceDispatchTable::GetPhysicalDeviceProperties>(mock_physical_device, &properties);
    EXPECT_TRUE(physical_device_function_called);
    EXPECT_EQ(properties.apiVersion, static_cast<uint32_t>(VK_API_VERSION_1_3));
}

