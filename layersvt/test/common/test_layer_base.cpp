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
#include "layer_base_test_peer.h"
#include "layer_test_helper.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace layersvt;

TEST(LayerBaseTest, LayerTracking) {
    EXPECT_EQ(LayerBase::Get(), nullptr);
    {
        LayerBase layer;
        EXPECT_EQ(LayerBase::Get(), &layer);
    }
    EXPECT_EQ(LayerBase::Get(), nullptr);

    {
        LayerBase layer1;
        EXPECT_EQ(LayerBase::Get(), &layer1);
    }
    EXPECT_EQ(LayerBase::Get(), nullptr);
}

TEST(LayerBaseTest, GetKnownCommandsCommon) {
    LayerBase layer;
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetInstanceProcAddr"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkNonExistentInstanceFunction"), nullptr);

    EXPECT_NE(LayerBaseTestPeer::GetKnownDeviceCommand("vkGetDeviceProcAddr"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkCreateDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkNonExistentDeviceFunction"), nullptr);
}

class TestDerivedLayer : public LayerBase {
   public:
    static inline auto mock_custom_instance_function =
        reinterpret_cast<PFN_vkVoidFunction>(static_cast<uintptr_t>(0x12345678));
    static inline auto mock_custom_device_function =
        reinterpret_cast<PFN_vkVoidFunction>(static_cast<uintptr_t>(0x87654321));

   protected:
    PFN_vkVoidFunction GetLayerInstanceCommand(const char* command_name) override {
        if (std::strcmp(command_name, "vkCustomInstanceCmd") == 0) {
            return mock_custom_instance_function;
        }
        return nullptr;
    }

    PFN_vkVoidFunction GetLayerDeviceCommand(const char* command_name) override {
        if (std::strcmp(command_name, "vkCustomDeviceCmd") == 0) {
            return mock_custom_device_function;
        }
        return nullptr;
    }
};

TEST(LayerBaseTest, LayerSpecificOverrideHooks) {
    TestDerivedLayer layer;

    // Custom commands handled by virtual hooks
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkCustomInstanceCmd"), TestDerivedLayer::mock_custom_instance_function);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkCustomDeviceCmd"), TestDerivedLayer::mock_custom_device_function);

    // Common command handled by base fallback
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetInstanceProcAddr"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownDeviceCommand("vkGetDeviceProcAddr"), nullptr);

    // Unhandled commands return nullptr
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkUnknownCmd"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkUnknownCmd"), nullptr);
}

TEST(LayerBaseTest, ProcAddrDispatchChain) {
    TestDerivedLayer layer;
    auto& dispatch_table_manager = LayerBaseTestPeer::GetDispatchTableManager(layer);

    // 1. Global commands can be queried with VK_NULL_HANDLE
    EXPECT_NE(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr"), nullptr);

    // Non-global commands must return nullptr when instance is VK_NULL_HANDLE
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkDestroyInstance"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCustomInstanceCmd"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCustomDeviceCmd"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkNextLayerCmd"), nullptr);

    // GetDeviceProcAddr with VK_NULL_HANDLE must always return nullptr
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkGetDeviceProcAddr"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkDestroyDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkCreateDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkCustomDeviceCmd"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(VK_NULL_HANDLE, "vkNextLayerCmd"), nullptr);

    // 2. Querying with valid instance handle
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    static auto mock_next_instance_command = reinterpret_cast<PFN_vkVoidFunction>(static_cast<uintptr_t>(0xABCDEF01));

    dispatch_table_manager.InitInstanceTable(mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkNextLayerInstCmd") == 0) {
            return mock_next_instance_command;
        }
        return nullptr;
    });

    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkCustomInstanceCmd"),
              TestDerivedLayer::mock_custom_instance_function);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkCustomDeviceCmd"),
              TestDerivedLayer::mock_custom_device_function);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkNextLayerInstCmd"), mock_next_instance_command);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkUnimplementedCmd"), nullptr);

    // 3. Querying with valid device handle
    void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x55667788));
    auto mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);
    static auto mock_next_device_command = reinterpret_cast<PFN_vkVoidFunction>(static_cast<uintptr_t>(0xABCDEF02));

    dispatch_table_manager.InitDeviceTable(mock_device, [](VkDevice, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkNextLayerDevCmd") == 0) {
            return mock_next_device_command;
        }
        return nullptr;
    });

    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkCustomDeviceCmd"),
              TestDerivedLayer::mock_custom_device_function);
    EXPECT_NE(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkGetDeviceProcAddr"), nullptr);
    // Instance commands must return nullptr via GetDeviceProcAddr even with valid device
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkCreateDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkDestroyInstance"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkNextLayerDevCmd"), mock_next_device_command);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkUnimplementedCmd"), nullptr);
}
