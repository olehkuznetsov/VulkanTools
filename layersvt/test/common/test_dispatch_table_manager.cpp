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

#include "common/dispatch_table_manager.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

using namespace layersvt;

TEST(DispatchTableManagerTest, GetDispatchKey) {
    void* mock_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEF));
    void* mock_object = &mock_vtable;

    EXPECT_EQ(DispatchTableManager::GetDispatchKey(mock_object),
              static_cast<DispatchTableManager::Key>(reinterpret_cast<uintptr_t>(mock_vtable)));
}

TEST(DispatchTableManagerTest, LoaderDataCallback) {
    DispatchTableManager dispatch_table_manager;

    void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x12345678));
    VkDevice mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);

    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(mock_device), nullptr);

    PFN_vkSetDeviceLoaderData dummy_callback = [](VkDevice, void*) -> VkResult { return VK_SUCCESS; };
    dispatch_table_manager.SetDeviceLoaderDataCallback(mock_device, dummy_callback);

    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(mock_device), dummy_callback);

    dispatch_table_manager.DestroyDeviceTable(DispatchTableManager::GetDispatchKey(mock_device));
    EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(mock_device), nullptr);
}

TEST(DispatchTableManagerTest, InstanceAndDeviceTableLifecycle) {
    DispatchTableManager dispatch_table_manager;

    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), nullptr);
    auto* instance_table = dispatch_table_manager.InitInstanceTable(
        mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
    EXPECT_NE(instance_table, nullptr);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), instance_table);

    dispatch_table_manager.DestroyInstanceTable(DispatchTableManager::GetDispatchKey(mock_instance));
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), nullptr);

    void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x2222));
    auto mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);

    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(mock_device), nullptr);
    auto* device_table =
        dispatch_table_manager.InitDeviceTable(mock_device, [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; });
    EXPECT_NE(device_table, nullptr);
    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(mock_device), device_table);

    dispatch_table_manager.DestroyDeviceTable(DispatchTableManager::GetDispatchKey(mock_device));
    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(mock_device), nullptr);
}

TEST(DispatchTableManagerTest, ReinitPreservesExistingTablePointerStability) {
    DispatchTableManager dispatch_table_manager;

    void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3333));
    auto mock_device = reinterpret_cast<VkDevice>(&mock_device_vtable);
    PFN_vkGetDeviceProcAddr get_device_proc_addr = [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; };

    auto* first_table = dispatch_table_manager.InitDeviceTable(mock_device, get_device_proc_addr);
    auto* second_table = dispatch_table_manager.InitDeviceTable(mock_device, get_device_proc_addr);
    EXPECT_EQ(second_table, first_table);
}

TEST(DispatchTableManagerTest, ConcurrentAccess) {
    DispatchTableManager dispatch_table_manager;

    constexpr int kNumberOfThreads = 8;
    constexpr int kIterations = 500;
    std::atomic<bool> start_flag{false};
    std::vector<std::thread> threads;

    // Pre-allocate dummy vtables and handles for each thread to ensure stable memory addresses
    struct ThreadMockData {
        void* instance_vtable;
        VkInstance instance;
        void* device_vtable;
        VkDevice device;
    };
    std::vector<ThreadMockData> mock_data(kNumberOfThreads);
    for (int thread_index = 0; thread_index < kNumberOfThreads; ++thread_index) {
        mock_data[thread_index].instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000 + thread_index * 0x100));
        mock_data[thread_index].instance = reinterpret_cast<VkInstance>(&mock_data[thread_index].instance_vtable);
        mock_data[thread_index].device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x20000 + thread_index * 0x100));
        mock_data[thread_index].device = reinterpret_cast<VkDevice>(&mock_data[thread_index].device_vtable);
    }

    PFN_vkSetDeviceLoaderData dummy_callback = [](VkDevice, void*) -> VkResult { return VK_SUCCESS; };

    for (int thread_index = 0; thread_index < kNumberOfThreads; ++thread_index) {
        threads.emplace_back([&, thread_index]() {
            while (!start_flag.load()) {
                std::this_thread::yield();
            }

            auto& my_data = mock_data[thread_index];

            for (int i = 0; i < kIterations; ++i) {
                // Initialize tables
                auto* instance_table = dispatch_table_manager.InitInstanceTable(
                    my_data.instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
                EXPECT_NE(instance_table, nullptr);

                auto* device_table = dispatch_table_manager.InitDeviceTable(
                    my_data.device, [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; });
                EXPECT_NE(device_table, nullptr);

                // Set loader data callback
                dispatch_table_manager.SetDeviceLoaderDataCallback(my_data.device, dummy_callback);

                // Read back own tables and callback
                EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(my_data.instance), instance_table);
                EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(my_data.device), device_table);
                EXPECT_EQ(dispatch_table_manager.GetDeviceLoaderDataCallback(my_data.device), dummy_callback);

                // Concurrent cross-thread read from a neighbor's handle
                int neighbor_index = (thread_index + 1) % kNumberOfThreads;
                (void)dispatch_table_manager.GetDeviceDispatchTable(mock_data[neighbor_index].device);
                (void)dispatch_table_manager.GetInstanceDispatchTable(mock_data[neighbor_index].instance);

                // Destroy tables periodically
                if ((i % 10) == 0) {
                    dispatch_table_manager.DestroyDeviceTable(DispatchTableManager::GetDispatchKey(my_data.device));
                    dispatch_table_manager.DestroyInstanceTable(DispatchTableManager::GetDispatchKey(my_data.instance));
                }
            }
        });
    }

    start_flag.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
}

TEST(DispatchTableManagerTest, BasicPhysicalDeviceTracking) {
    DispatchTableManager dispatch_table_manager;

    auto mock_instance = reinterpret_cast<VkInstance>(static_cast<uintptr_t>(0x1000));
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2001));
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2002));

    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device1), VK_NULL_HANDLE);

    dispatch_table_manager.SetVkInstance(mock_physical_device1, mock_instance);
    dispatch_table_manager.SetVkInstance(mock_physical_device2, mock_instance);

    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device1), mock_instance);
    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device2), mock_instance);
}

TEST(DispatchTableManagerTest, RegisterPhysicalDevicesBatch) {
    DispatchTableManager dispatch_table_manager;

    auto mock_instance = reinterpret_cast<VkInstance>(static_cast<uintptr_t>(0x1000));
    std::vector<VkPhysicalDevice> physical_devices = {
        reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2001)),
        reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2002)),
        reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2003)),
    };

    dispatch_table_manager.RegisterPhysicalDevices(physical_devices.data(), static_cast<uint32_t>(physical_devices.size()), mock_instance);

    EXPECT_EQ(dispatch_table_manager.GetVkInstance(physical_devices[0]), mock_instance);
    EXPECT_EQ(dispatch_table_manager.GetVkInstance(physical_devices[1]), mock_instance);
    EXPECT_EQ(dispatch_table_manager.GetVkInstance(physical_devices[2]), mock_instance);
}

TEST(DispatchTableManagerTest, PhysicalDeviceResolvesInstanceDispatchTable) {
    DispatchTableManager dispatch_table_manager;

    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2222));

    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_physical_device), nullptr);

    auto* instance_table = dispatch_table_manager.InitInstanceTable(
        mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
    EXPECT_NE(instance_table, nullptr);

    dispatch_table_manager.SetVkInstance(mock_physical_device, mock_instance);

    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_physical_device), instance_table);
}

TEST(DispatchTableManagerTest, AtomicTeardownOfPhysicalDevicesOnInstanceDestroy) {
    DispatchTableManager dispatch_table_manager;

    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2001));
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x2002));

    auto* instance_table = dispatch_table_manager.InitInstanceTable(
        mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
    ASSERT_NE(instance_table, nullptr);

    dispatch_table_manager.SetVkInstance(mock_physical_device1, mock_instance);
    dispatch_table_manager.SetVkInstance(mock_physical_device2, mock_instance);

    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device1), mock_instance);
    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device2), mock_instance);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_physical_device1), instance_table);

    auto dispatch_key = DispatchTableManager::GetDispatchKey(mock_instance);
    dispatch_table_manager.DestroyInstanceTable(dispatch_key);

    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_instance), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(mock_physical_device1), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device1), VK_NULL_HANDLE);
    EXPECT_EQ(dispatch_table_manager.GetVkInstance(mock_physical_device2), VK_NULL_HANDLE);
}

TEST(DispatchTableManagerTest, NullHandleSafety) {
    DispatchTableManager dispatch_table_manager;

    EXPECT_EQ(dispatch_table_manager.GetVkInstance(VK_NULL_HANDLE), VK_NULL_HANDLE);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(static_cast<VkInstance>(VK_NULL_HANDLE)), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(static_cast<VkPhysicalDevice>(VK_NULL_HANDLE)), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(nullptr), nullptr);
    EXPECT_EQ(dispatch_table_manager.GetDeviceDispatchTable(static_cast<const void*>(nullptr)), nullptr);
}

TEST(DispatchTableManagerTest, ConcurrentPhysicalDevicesAndLifecycle) {
    DispatchTableManager dispatch_table_manager;

    constexpr int kNumberOfThreads = 8;
    constexpr int kIterations = 300;
    std::atomic<bool> start_flag{false};
    std::vector<std::thread> threads;

    struct ThreadPhysicalMockData {
        void* instance_vtable;
        VkInstance instance;
        VkPhysicalDevice physical_device1;
        VkPhysicalDevice physical_device2;
    };

    std::vector<ThreadPhysicalMockData> mock_data(kNumberOfThreads);
    for (int thread_index = 0; thread_index < kNumberOfThreads; ++thread_index) {
        mock_data[thread_index].instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x30000 + thread_index * 0x100));
        mock_data[thread_index].instance = reinterpret_cast<VkInstance>(&mock_data[thread_index].instance_vtable);
        mock_data[thread_index].physical_device1 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x40000 + thread_index * 0x20));
        mock_data[thread_index].physical_device2 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x40001 + thread_index * 0x20));
    }

    for (int thread_index = 0; thread_index < kNumberOfThreads; ++thread_index) {
        threads.emplace_back([&, thread_index]() {
            while (!start_flag.load()) {
                std::this_thread::yield();
            }

            auto& my_data = mock_data[thread_index];

            for (int i = 0; i < kIterations; ++i) {
                auto* instance_table = dispatch_table_manager.InitInstanceTable(
                    my_data.instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });
                EXPECT_NE(instance_table, nullptr);

                dispatch_table_manager.SetVkInstance(my_data.physical_device1, my_data.instance);
                dispatch_table_manager.SetVkInstance(my_data.physical_device2, my_data.instance);

                EXPECT_EQ(dispatch_table_manager.GetVkInstance(my_data.physical_device1), my_data.instance);
                EXPECT_EQ(dispatch_table_manager.GetInstanceDispatchTable(my_data.physical_device1), instance_table);

                int neighbor_index = (thread_index + 1) % kNumberOfThreads;
                (void)dispatch_table_manager.GetVkInstance(mock_data[neighbor_index].physical_device1);
                (void)dispatch_table_manager.GetInstanceDispatchTable(mock_data[neighbor_index].physical_device1);

                if ((i % 10) == 0) {
                    dispatch_table_manager.DestroyInstanceTable(DispatchTableManager::GetDispatchKey(my_data.instance));
                    EXPECT_EQ(dispatch_table_manager.GetVkInstance(my_data.physical_device1), VK_NULL_HANDLE);
                }
            }
        });
    }

    start_flag.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
}

