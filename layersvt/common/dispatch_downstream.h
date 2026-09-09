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

#pragma once

#include "layer_base.h"
#include <vulkan/vulkan.h>
#include <vulkan/utility/vk_dispatch_table.h>
#include <cassert>
#include <type_traits>
#include <utility>

namespace layersvt {

// Trait helper to extract the class type from a member pointer
template <typename MemberType>
struct MemberTraits {
    static_assert(std::is_member_pointer_v<MemberType>,
                  "MemberPointer must be a pointer to a member of VkuInstanceDispatchTable or VkuDeviceDispatchTable");
};

template <typename Class, typename Member>
struct MemberTraits<Member Class::*> {
    using ClassType = Class;
    using MemberType = Member;
};

/**
 * Forwards a required Vulkan command downstream using the dispatch table.
 * Deduces instance vs. device table and asserts the entry point is non-null.
 * Returns the result of calling the downstream Vulkan command.
 */
template <auto MemberPointer, typename Handle, typename... Args>
inline auto DispatchDownstream(Handle handle, Args&&... args) {
    using TableType = typename MemberTraits<decltype(MemberPointer)>::ClassType;
    static_assert(std::is_same_v<TableType, VkuInstanceDispatchTable> ||
                  std::is_same_v<TableType, VkuDeviceDispatchTable>,
                  "MemberPointer must be a member of VkuInstanceDispatchTable or VkuDeviceDispatchTable");
    TableType* table = nullptr;
    if constexpr (std::is_same_v<TableType, VkuInstanceDispatchTable>) {
        table = LayerBase::GetInstanceDispatchTable(handle);
    } else {
        table = LayerBase::GetDeviceDispatchTable(handle);
    }

    assert(table != nullptr && "Dispatch table must exist for valid handle");
    assert(table->*MemberPointer != nullptr && "Function pointer must exist in dispatch table");
    return (table->*MemberPointer)(handle, std::forward<Args>(args)...);
}

/**
 * Forwards a Vulkan command downstream, returning or invoking fallback if the entry point is null.
 * Returns the downstream command result on success, or the evaluated fallback value if the table
 * or command pointer is unavailable.
 */
template <auto MemberPointer, typename Fallback, typename Handle, typename... Args>
inline auto DispatchDownstreamOr(Fallback&& fallback, Handle handle, Args&&... args) {
    using TableType = typename MemberTraits<decltype(MemberPointer)>::ClassType;
    static_assert(std::is_same_v<TableType, VkuInstanceDispatchTable> ||
                  std::is_same_v<TableType, VkuDeviceDispatchTable>,
                  "MemberPointer must be a member of VkuInstanceDispatchTable or VkuDeviceDispatchTable");
    using ReturnType = decltype((std::declval<TableType*>()->*MemberPointer)(handle, std::forward<Args>(args)...));
    TableType* table = nullptr;
    if constexpr (std::is_same_v<TableType, VkuInstanceDispatchTable>) {
        table = LayerBase::GetInstanceDispatchTable(handle);
    } else {
        table = LayerBase::GetDeviceDispatchTable(handle);
    }

    if (table && table->*MemberPointer) {
        return static_cast<ReturnType>((table->*MemberPointer)(handle, std::forward<Args>(args)...));
    }

    if constexpr (std::is_invocable_v<Fallback>) {
        return static_cast<ReturnType>(std::forward<Fallback>(fallback)());
    } else {
        return static_cast<ReturnType>(std::forward<Fallback>(fallback));
    }
}

/**
 * Forwards an optional Vulkan command returning VkResult downstream, falling back to VK_SUCCESS.
 * Returns the downstream VkResult on success, or VK_SUCCESS if the downstream command is unavailable.
 */
template <auto MemberPointer, typename Handle, typename... Args>
inline VkResult DispatchDownstreamOrSuccess(Handle handle, Args&&... args) {
    using TableType = typename MemberTraits<decltype(MemberPointer)>::ClassType;
    using ReturnType = decltype((std::declval<TableType*>()->*MemberPointer)(handle, std::forward<Args>(args)...));
    static_assert(std::is_same_v<ReturnType, VkResult>,
                  "DispatchDownstreamOrSuccess can only be used with Vulkan commands returning VkResult");
    return DispatchDownstreamOr<MemberPointer>(VK_SUCCESS, handle, std::forward<Args>(args)...);
}

}  // namespace layersvt
