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

class LifecycleTestLayer : public LayerBase {
   public:
    bool pre_create_instance_called = false;
    bool post_create_instance_called = false;
    bool pre_destroy_instance_called = false;

    bool pre_create_device_called = false;
    bool post_create_device_called = false;
    bool pre_destroy_device_called = false;

    const VkAllocationCallbacks* captured_post_create_instance_allocator = nullptr;
    const VkAllocationCallbacks* captured_post_create_device_allocator = nullptr;

    void PreCreateInstance(VkInstanceCreateInfo*, const VkAllocationCallbacks*) override { pre_create_instance_called = true; }
    void PostCreateInstance(VkInstance, const VkInstanceCreateInfo*, const VkAllocationCallbacks* allocator) override {
        post_create_instance_called = true;
        captured_post_create_instance_allocator = allocator;
    }
    void PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) override { pre_destroy_instance_called = true; }

    void PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo*, const VkAllocationCallbacks*) override {
        pre_create_device_called = true;
    }
    void PostCreateDevice(VkDevice, VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks* allocator) override {
        post_create_device_called = true;
        captured_post_create_device_allocator = allocator;
    }
    void PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) override { pre_destroy_device_called = true; }
};

TEST(LayerBaseTest, HookInvocations) {
    LifecycleTestLayer layer;
    EXPECT_FALSE(layer.pre_create_instance_called);
    EXPECT_FALSE(layer.pre_create_device_called);
    EXPECT_FALSE(layer.post_create_device_called);

    // Verify hooks trigger as expected
    layer.PreCreateInstance(nullptr, nullptr);
    EXPECT_TRUE(layer.pre_create_instance_called);

    layer.PreCreateDevice(VK_NULL_HANDLE, nullptr, nullptr);
    EXPECT_TRUE(layer.pre_create_device_called);

    layer.PostCreateDevice(VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, nullptr);
    EXPECT_TRUE(layer.post_create_device_called);
}

TEST(LayerBaseTest, CreateInstanceWithMockChain) {
    static void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    static auto mock_instance_handle = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkCreateInstance") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(
                +[](const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance_handle) -> VkResult {
                    *instance_handle = mock_instance_handle;
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
    LifecycleTestLayer layer;
    VkInstance instance = VK_NULL_HANDLE;

    VkAllocationCallbacks mock_allocator{};
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(&instance_create_info, &mock_allocator, &instance), VK_SUCCESS);
    EXPECT_EQ(instance, mock_instance_handle);
    EXPECT_TRUE(layer.pre_create_instance_called);
    EXPECT_TRUE(layer.post_create_instance_called);
    EXPECT_EQ(layer.captured_post_create_instance_allocator, &mock_allocator);
    EXPECT_NE(LayerBaseTestPeer::GetDispatchTableManager(layer).GetInstanceDispatchTable(instance), nullptr);

    LayerBaseTestPeer::DestroyInstance(instance, nullptr);
    EXPECT_TRUE(layer.pre_destroy_instance_called);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetInstanceDispatchTable(instance), nullptr);
}

TEST(LayerBaseTest, CreateInstanceNullHandling) {
    LifecycleTestLayer layer;
    VkInstance instance = VK_NULL_HANDLE;
    // Null create info
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(nullptr, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);

    // Missing chain info
    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(&instance_create_info, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, PreCreateNotInvokedOnMissingChain) {
    LifecycleTestLayer layer;
    VkInstance instance = VK_NULL_HANDLE;
    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(&instance_create_info, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_FALSE(layer.pre_create_instance_called);
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
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(nullptr, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_FALSE(layer.inspected);

    VkInstanceCreateInfo instance_create_info{};
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(&instance_create_info, nullptr, nullptr), VK_ERROR_INITIALIZATION_FAILED);
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
    static void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
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

    MutatingLayer layer;
    VkInstance instance = VK_NULL_HANDLE;
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(&instance_create_info, nullptr, &instance), VK_SUCCESS);
    EXPECT_EQ(received_flags, 0xABCDu);
    LayerBaseTestPeer::DestroyInstance(instance, nullptr);
}

TEST(LayerBaseTest, PreCreateDeviceMutation) {
    class MutatingLayer : public LayerBase {
       public:
        void PreCreateDevice(VkPhysicalDevice, VkDeviceCreateInfo* create_info, const VkAllocationCallbacks*) override {
            if (create_info) {
                create_info->flags = 0x5678;
            }
        }
    };

    static VkDeviceCreateFlags received_flags = 0;
    static void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x55667788));
    static auto mock_device_handle = reinterpret_cast<VkDevice>(&mock_device_vtable);

    PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
        if (std::strcmp(function_name, "vkCreateDevice") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(+[](VkPhysicalDevice, const VkDeviceCreateInfo* create_info,
                                                            const VkAllocationCallbacks*, VkDevice* device_handle) -> VkResult {
                received_flags = create_info->flags;
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

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &chain_info;

    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x5555));

    MutatingLayer layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).SetVkInstance(mock_physical_device, mock_instance);
    VkDevice device = VK_NULL_HANDLE;

    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_physical_device, &device_create_info, nullptr, &device), VK_SUCCESS);
    EXPECT_EQ(received_flags, 0x5678u);

    LayerBaseTestPeer::DestroyDevice(device, nullptr);
}

TEST(LayerBaseTest, TeardownOrdering) {
    class TeardownOrderLayer : public LayerBase {
       public:
        VkInstance captured_instance_in_pre_destroy = VK_NULL_HANDLE;
        VkPhysicalDevice mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x9999));

        void PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) override {
            captured_instance_in_pre_destroy = LayerBaseTestPeer::GetVkInstance(mock_physical_device);
        }
    };

    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x9999));

    TeardownOrderLayer layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).InitInstanceTable(
        mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
            if (std::strcmp(function_name, "vkDestroyInstance") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(+[](VkInstance, const VkAllocationCallbacks*) {});
            }
            return nullptr;
        });
    LayerBaseTestPeer::GetDispatchTableManager(layer).SetVkInstance(mock_physical_device, mock_instance);

    LayerBaseTestPeer::DestroyInstance(mock_instance, nullptr);

    // Verify PreDestroyInstance could still query the physical device mapping
    EXPECT_EQ(layer.captured_instance_in_pre_destroy, mock_instance);
    // After DestroyInstance finishes, mapping is cleaned up
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetVkInstance(mock_physical_device), VK_NULL_HANDLE);
}

TEST(LayerBaseTest, CreateDeviceWithMockChain) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x5555));

    static void* mock_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x55667788));
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

    LifecycleTestLayer layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).SetVkInstance(mock_physical_device, mock_instance);
    VkDevice device = VK_NULL_HANDLE;

    VkAllocationCallbacks mock_allocator{};
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_physical_device, &device_create_info, &mock_allocator, &device),
              VK_SUCCESS);
    EXPECT_EQ(device, mock_device_handle);
    EXPECT_TRUE(layer.pre_create_device_called);
    EXPECT_TRUE(layer.post_create_device_called);
    EXPECT_EQ(layer.captured_post_create_device_allocator, &mock_allocator);
    EXPECT_NE(LayerBaseTestPeer::GetDispatchTableManager(layer).GetDeviceDispatchTable(device), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetDeviceLoaderDataCallback(device), mock_loader_callback);

    LayerBaseTestPeer::DestroyDevice(device, nullptr);
    EXPECT_TRUE(layer.pre_destroy_device_called);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetDeviceDispatchTable(device), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetDeviceLoaderDataCallback(device), nullptr);
}

TEST(LayerBaseTest, CreateDeviceNullHandling) {
    LayerBase layer;
    VkDevice device = VK_NULL_HANDLE;
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x5555));
    LayerBaseTestPeer::GetDispatchTableManager(layer).SetVkInstance(mock_physical_device, mock_instance);

    // Null create info
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_physical_device, nullptr, nullptr, &device),
              VK_ERROR_INITIALIZATION_FAILED);

    // Missing chain info
    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_physical_device, &device_create_info, nullptr, &device),
              VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, CreateDeviceInvalidInputSafety) {
    LayerBase layer;
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    auto mock_untracked_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0xBAADF00D));
    // Untracked physical device must fail cleanly without crashing
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_untracked_physical_device, &create_info, nullptr, &device),
              VK_ERROR_INITIALIZATION_FAILED);

    // Null device pointer must fail cleanly
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_untracked_physical_device, &create_info, nullptr, nullptr),
              VK_ERROR_INITIALIZATION_FAILED);

    // VK_NULL_HANDLE physical device must fail cleanly
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(VK_NULL_HANDLE, &create_info, nullptr, &device),
              VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, EnumeratePhysicalDevicesMapping) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x7777));

    LayerBase layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).InitInstanceTable(
        mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
            if (std::strcmp(function_name, "vkEnumeratePhysicalDevices") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(
                    +[](VkInstance, uint32_t* physical_device_count, VkPhysicalDevice* physical_devices) -> VkResult {
                        if (!physical_device_count) return VK_ERROR_INITIALIZATION_FAILED;
                        if (!physical_devices) {
                            *physical_device_count = 1;
                            return VK_SUCCESS;
                        }
                        physical_devices[0] = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x7777));
                        *physical_device_count = 1;
                        return VK_SUCCESS;
                    });
            }
            return nullptr;
        });

    uint32_t count = 0;
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDevices(mock_instance, &count, nullptr), VK_SUCCESS);
    ASSERT_EQ(count, 1u);

    std::vector<VkPhysicalDevice> physical_devices(count);
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDevices(mock_instance, &count, physical_devices.data()), VK_SUCCESS);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetVkInstance(mock_physical_device), mock_instance);
}

class HookTestLayer : public LayerBase {
   public:
    using LayerBase::LayerBase;
    using LayerBase::PostCreateDevice;
    using LayerBase::PostCreateInstance;
    using LayerBase::PreCreateDevice;
    using LayerBase::PreCreateInstance;
    using LayerBase::PreDestroyDevice;
    using LayerBase::PreDestroyInstance;
};

TEST(LayerBaseTest, DefaultHooksExecution) {
    HookTestLayer base;

    // Execute default no-op hooks to verify base class behavior
    VkInstanceCreateInfo instance_create_info{};
    base.PreCreateInstance(&instance_create_info, nullptr);
    base.PostCreateInstance(VK_NULL_HANDLE, &instance_create_info, nullptr);
    base.PreDestroyInstance(VK_NULL_HANDLE, nullptr);

    VkDeviceCreateInfo device_create_info{};
    base.PreCreateDevice(VK_NULL_HANDLE, &device_create_info, nullptr);
    base.PostCreateDevice(VK_NULL_HANDLE, VK_NULL_HANDLE, &device_create_info, nullptr);
    base.PreDestroyDevice(VK_NULL_HANDLE, nullptr);
}

TEST(LayerBaseTest, EnumeratePhysicalDeviceGroupsMapping) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device1 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x8881));
    auto mock_physical_device2 = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x8882));

    LayerBase layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).InitInstanceTable(
        mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
            if (std::strcmp(function_name, "vkEnumeratePhysicalDeviceGroups") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(
                    +[](VkInstance, uint32_t* physical_device_group_count,
                        VkPhysicalDeviceGroupProperties* physical_device_groups) -> VkResult {
                        if (!physical_device_group_count) return VK_ERROR_INITIALIZATION_FAILED;
                        if (!physical_device_groups) {
                            *physical_device_group_count = 1;
                            return VK_SUCCESS;
                        }
                        physical_device_groups[0].physicalDeviceCount = 2;
                        physical_device_groups[0].physicalDevices[0] =
                            reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x8881));
                        physical_device_groups[0].physicalDevices[1] =
                            reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x8882));
                        *physical_device_group_count = 1;
                        return VK_SUCCESS;
                    });
            }
            return nullptr;
        });

    void* missing_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0xBAADF00D));
    auto missing_instance = reinterpret_cast<VkInstance>(&missing_instance_vtable);
    // Missing table or function
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDeviceGroups(missing_instance, nullptr, nullptr),
              VK_ERROR_INITIALIZATION_FAILED);

    uint32_t count = 0;
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDeviceGroups(mock_instance, &count, nullptr), VK_SUCCESS);
    ASSERT_EQ(count, 1u);

    std::vector<VkPhysicalDeviceGroupProperties> groups(count);
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDeviceGroups(mock_instance, &count, groups.data()), VK_SUCCESS);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetVkInstance(mock_physical_device1), mock_instance);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetVkInstance(mock_physical_device2), mock_instance);
}

TEST(LayerBaseTest, EnumeratePhysicalDeviceGroupsKHRFallbackMapping) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x8883));

    LayerBase layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).InitInstanceTable(
        mock_instance, [](VkInstance, const char* function_name) -> PFN_vkVoidFunction {
            if (std::strcmp(function_name, "vkEnumeratePhysicalDeviceGroupsKHR") == 0) {
                return reinterpret_cast<PFN_vkVoidFunction>(
                    +[](VkInstance, uint32_t* physical_device_group_count,
                        VkPhysicalDeviceGroupProperties* physical_device_groups) -> VkResult {
                        if (!physical_device_group_count) return VK_ERROR_INITIALIZATION_FAILED;
                        if (!physical_device_groups) {
                            *physical_device_group_count = 1;
                            return VK_SUCCESS;
                        }
                        physical_device_groups[0].physicalDeviceCount = 1;
                        physical_device_groups[0].physicalDevices[0] =
                            reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x8883));
                        *physical_device_group_count = 1;
                        return VK_SUCCESS;
                    });
            }
            return nullptr;
        });

    uint32_t count = 0;
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDeviceGroups(mock_instance, &count, nullptr), VK_SUCCESS);
    ASSERT_EQ(count, 1u);

    std::vector<VkPhysicalDeviceGroupProperties> groups(count);
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDeviceGroups(mock_instance, &count, groups.data()), VK_SUCCESS);
    EXPECT_EQ(LayerBaseTestPeer::GetDispatchTableManager(layer).GetVkInstance(mock_physical_device), mock_instance);
}

TEST(LayerBaseTest, EnumeratePhysicalDevicesNullTable) {
    LayerBase layer;
    void* missing_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0xBAADF00D));
    auto missing_instance = reinterpret_cast<VkInstance>(&missing_instance_vtable);
    uint32_t count = 0;
    EXPECT_EQ(LayerBaseTestPeer::EnumeratePhysicalDevices(missing_instance, &count, nullptr),
              VK_ERROR_INITIALIZATION_FAILED);
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

    LifecycleTestLayer layer;
    VkInstance instance = VK_NULL_HANDLE;
    EXPECT_EQ(LayerBaseTestPeer::CreateInstance(&instance_create_info, nullptr, &instance), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(LayerBaseTest, CreateDeviceNullFpCreateDevice) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x5555));

    LifecycleTestLayer layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).SetVkInstance(mock_physical_device, mock_instance);

    static PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr = [](VkInstance, const char*) -> PFN_vkVoidFunction {
        return nullptr;
    };
    static PFN_vkGetDeviceProcAddr mock_get_device_proc_addr = [](VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; };
    VkLayerDeviceLink layer_link{nullptr, mock_get_instance_proc_addr, mock_get_device_proc_addr};
    VkLayerDeviceCreateInfo chain_info{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, nullptr, VK_LAYER_LINK_INFO, {&layer_link}};

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &chain_info;

    VkDevice device = VK_NULL_HANDLE;
    EXPECT_EQ(LayerBaseTestPeer::CreateDevice(mock_physical_device, &device_create_info, nullptr, &device),
              VK_ERROR_INITIALIZATION_FAILED);
}

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
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateInstance"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkDestroyInstance"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumeratePhysicalDevices"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkEnumeratePhysicalDeviceGroups"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkNonExistentInstanceFunction"), nullptr);

    EXPECT_NE(LayerBaseTestPeer::GetKnownDeviceCommand("vkGetDeviceProcAddr"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownDeviceCommand("vkDestroyDevice"), nullptr);
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

    // Common commands still handled by base template method fallback
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkGetInstanceProcAddr"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateInstance"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownInstanceCommand("vkCreateDevice"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownDeviceCommand("vkGetDeviceProcAddr"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetKnownDeviceCommand("vkDestroyDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkCreateDevice"), nullptr);

    // Unhandled commands return nullptr
    EXPECT_EQ(LayerBaseTestPeer::GetKnownInstanceCommand("vkUnknownCmd"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetKnownDeviceCommand("vkUnknownCmd"), nullptr);
}

TEST(LayerBaseTest, ProcAddrDispatchChain) {
    TestDerivedLayer layer;
    auto& dispatch_table_manager = LayerBaseTestPeer::GetDispatchTableManager(layer);

    // 1. Global commands can be queried with VK_NULL_HANDLE
    EXPECT_NE(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"), nullptr);

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
    EXPECT_NE(LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkCreateDevice"), nullptr);
    EXPECT_NE(LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkDestroyInstance"), nullptr);
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
    EXPECT_NE(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkDestroyDevice"), nullptr);
    // Instance commands must return nullptr via GetDeviceProcAddr even with valid device
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkCreateDevice"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkDestroyInstance"), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkNextLayerDevCmd"), mock_next_device_command);
    EXPECT_EQ(LayerBaseTestPeer::GetDeviceProcAddr(mock_device, "vkUnimplementedCmd"), nullptr);
}

TEST(LayerBaseTest, DestroyNullHandles) {
    class DestroyTrackingLayer : public LayerBase {
       public:
        int pre_destroy_instance_calls = 0;
        int pre_destroy_device_calls = 0;

       protected:
        void PreDestroyInstance(VkInstance, const VkAllocationCallbacks*) override {
            ADD_FAILURE() << "PreDestroyInstance should not be called for VK_NULL_HANDLE";
            ++pre_destroy_instance_calls;
        }
        void PreDestroyDevice(VkDevice, const VkAllocationCallbacks*) override {
            ADD_FAILURE() << "PreDestroyDevice should not be called for VK_NULL_HANDLE";
            ++pre_destroy_device_calls;
        }
    };

    DestroyTrackingLayer layer;
    // Vulkan specification mandates destroying VK_NULL_HANDLE is a valid no-op.
    // Virtual PreDestroy hooks are bypassed when destroying VK_NULL_HANDLE.
    LayerBaseTestPeer::DestroyInstance(VK_NULL_HANDLE, nullptr);
    EXPECT_EQ(layer.pre_destroy_instance_calls, 0);

    LayerBaseTestPeer::DestroyDevice(VK_NULL_HANDLE, nullptr);
    EXPECT_EQ(layer.pre_destroy_device_calls, 0);
}

TEST(LayerBaseTest, PhysicalDeviceResolvesInstanceTable) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);
    void* mock_physical_device_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x55667788));
    auto mock_physical_device = reinterpret_cast<VkPhysicalDevice>(&mock_physical_device_vtable);

    LayerBase layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).SetVkInstance(mock_physical_device, mock_instance);
    LayerBaseTestPeer::GetDispatchTableManager(layer).InitInstanceTable(
        mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });

    EXPECT_NE(LayerBaseTestPeer::GetInstanceDispatchTable(mock_physical_device), nullptr);
    EXPECT_EQ(LayerBaseTestPeer::GetInstanceDispatchTable(mock_physical_device),
              LayerBaseTestPeer::GetInstanceDispatchTable(mock_instance));
}

TEST(LayerBaseTest, EnumeratePhysicalDeviceGroupsKHRResolution) {
    void* mock_instance_vtable = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11223344));
    auto mock_instance = reinterpret_cast<VkInstance>(&mock_instance_vtable);

    LayerBase layer;
    LayerBaseTestPeer::GetDispatchTableManager(layer).InitInstanceTable(
        mock_instance, [](VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; });

    PFN_vkVoidFunction function_khr =
        LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkEnumeratePhysicalDeviceGroupsKHR");
    PFN_vkVoidFunction function_core =
        LayerBaseTestPeer::GetInstanceProcAddr(mock_instance, "vkEnumeratePhysicalDeviceGroups");
    EXPECT_NE(function_khr, nullptr);
    EXPECT_EQ(function_khr, function_core);
}

TEST(LayerBaseTest, ResetLayerMultipleInvocations) {
    layer_test::ResetLayer<HookTestLayer>();
    EXPECT_NE(LayerBase::Get(), nullptr);
    layer_test::ResetLayer<HookTestLayer>();
    EXPECT_NE(LayerBase::Get(), nullptr);
    layer_test::ResetLayer<HookTestLayer>(/*destroy=*/true);
    EXPECT_EQ(LayerBase::Get(), nullptr);
}

TEST(LayerBaseTest, ResetLayerCrossType) {
    layer_test::ResetLayer<HookTestLayer>();
    EXPECT_NE(LayerBase::Get(), nullptr);
    layer_test::ResetLayer<LifecycleTestLayer>();
    EXPECT_NE(LayerBase::Get(), nullptr);
    layer_test::ResetLayer<HookTestLayer>();
    EXPECT_NE(LayerBase::Get(), nullptr);
    layer_test::ResetLayer<HookTestLayer>(/*destroy=*/true);
    EXPECT_EQ(LayerBase::Get(), nullptr);
}
