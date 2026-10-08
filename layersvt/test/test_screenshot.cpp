/*
 * Copyright (c) 2023 The Khronos Group Inc.
 * Copyright (c) 2023 Valve Corporation
 * Copyright (c) 2023 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Author: Christophe Riccio <christophe@lunarg.com>
 */

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_beta.h>

#include <gtest/gtest.h>
#include "layer_test_helper.h"

#include <cstdarg>

static const char* kLayerName = "VK_LAYER_LUNARG_screenshot";

class ScreenshotTests : public VkTestFramework {
   public:
    ~ScreenshotTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(ScreenshotTests, init_layer) {
    TEST_DESCRIPTION("Test Creating a Vulkan Instance with a layer");

    VkBool32 file = VK_TRUE;
    const char* frames = "8-2";
    const char* format = "UNORM";

    const std::vector<VkLayerSettingEXT> settings = {{kLayerName, "frames", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &frames},
                                                     {kLayerName, "format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format}};

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(settings);
    EXPECT_EQ(err, VK_SUCCESS);
}

TEST_F(ScreenshotTests, get_device_queue2) {
    TEST_DESCRIPTION("Test vkGetDeviceQueue2 forwards VkDeviceQueueInfo2 flags instead of downgrading to vkGetDeviceQueue");

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(kLayerName);
    ASSERT_EQ(err, VK_SUCCESS);

    VkPhysicalDevice phys_dev = VK_NULL_HANDLE;
    err = inst_builder.GetPhysicalDevice(&phys_dev);
    if (err != VK_SUCCESS || phys_dev == VK_NULL_HANDLE) {
        GTEST_SKIP() << "No Vulkan physical device available";
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = 0;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    VkDevice device = VK_NULL_HANDLE;
    err = vkCreateDevice(phys_dev, &device_create_info, nullptr, &device);
    ASSERT_EQ(err, VK_SUCCESS);
    ASSERT_NE(device, VK_NULL_HANDLE);

    VkDeviceQueueInfo2 queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    queue_info.flags = 0;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueIndex = 0;

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue2(device, &queue_info, &queue);
    EXPECT_NE(queue, VK_NULL_HANDLE);

    // Querying a queue with mismatched flags (protected when created unprotected) must forward
    // pQueueInfo to the driver's vkGetDeviceQueue2 rather than stripping flags via vkGetDeviceQueue.
    VkDeviceQueueInfo2 protected_queue_info = queue_info;
    protected_queue_info.flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
    VkQueue protected_queue = queue;
    vkGetDeviceQueue2(device, &protected_queue_info, &protected_queue);
    EXPECT_EQ(protected_queue, VK_NULL_HANDLE);

    vkDestroyDevice(device, nullptr);
}
