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

#include "device_memory_report.h"
#include "device_memory_report_perfetto.h"
#include "perfetto/perfetto.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

DeviceMemoryReport& DeviceMemoryReport::Get() {
    static DeviceMemoryReport instance;
    return instance;
}

void DeviceMemoryReport::SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    vk_instance_map_[phys_dev] = instance;
}

VkInstance DeviceMemoryReport::GetVkInstance(VkPhysicalDevice phys_dev) {
    if (phys_dev == VK_NULL_HANDLE) return VK_NULL_HANDLE;
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = vk_instance_map_.find(phys_dev);
    if (it != vk_instance_map_.end()) return it->second;

    // Fallback: If only a single VkInstance exists in the process, any valid physical device
    // must belong to that instance.
    if (vk_instance_map_.size() == 1) {
        return vk_instance_map_.begin()->second;
    }
    return VK_NULL_HANDLE;
}

void DeviceMemoryReport::SetDeviceMemoryProperties(VkDevice device, const VkPhysicalDeviceMemoryProperties& props) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    device_memory_properties_map_[device] = props;
}

void DeviceMemoryReport::SetAllocationMemoryProperties(uint64_t memory_handle, VkMemoryPropertyFlags property_flags) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    const AllocationKey key = AllocationKey::App(memory_handle);
    auto& allocation = memory_allocations_[key];
    allocation.mem_flags = property_flags;
    // An allocation with total_size > 0 represents an active physical memory allocation that
    // has already been recorded and contributed to Perfetto counter tracks.
    // Changing its memory property flags (e.g., HOST_VISIBLE or LAZILY_ALLOCATED) can alter
    // how its unbound memory headroom is classified into usage categories (such as staging
    // vs general buffer or transient memoryless). We must update the unbound counter to move
    // the reported bytes from the old usage track to the newly classified track.
    // If total_size == 0, the allocation has not yet been instantiated (or is being configured
    // prior to allocation), so no counter bytes have been emitted to Perfetto yet.
    if (allocation.total_size > 0) {
        UpdateAllocationUnboundCounter(key);
    }
}

void DeviceMemoryReport::OnDestroyDevice(VkDevice device) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    has_callback_map_.erase(device);
    device_memory_properties_map_.erase(device);
}

void VKAPI_PTR DeviceMemoryReport::MemoryReportCallback(const VkDeviceMemoryReportCallbackDataEXT* pCallbackData, void* pUserData) {
    DeviceMemoryReport::Get().OnMemoryReportEvent(pCallbackData);
}

const char* GetBufferCluster(VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags) {
    const VkBufferUsageFlags kFunctionalBufferUsageFlags =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;

    // 1. Host staging: Must be host-visible and not a primary GPU functional buffer
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) &&
        !(usage & kFunctionalBufferUsageFlags)) {
        return "staging_transfer";
    }
    // 2. Ray tracing acceleration structures & shader binding tables
    if (usage & (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)) {
        return "ray_tracing";
    }
    // 3. Indirect draw / dispatch commands & conditional rendering
    if (usage & (VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)) {
        return "indirect_gpu_driven";
    }
    // 4. Compute storage buffers (SSBOs & Storage Texel Buffers)
    if (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) {
        return "compute_storage";
    }
    // 5. Uniform buffers & uniform texel buffers
    if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) {
        return "uniform_constants";
    }
    // 6. Geometry & mesh data (Vertex + Index)
    if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        return "geometry_mesh";
    }
    // 7. Transform feedback stream-out
    if (usage & (VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT)) {
        return "transform_feedback";
    }
    return "general_buffer";
}

const char* GetImageCluster(VkImageUsageFlags usage, VkMemoryPropertyFlags memFlags) {
    // 1. Mobile TBDR on-chip tile memoryless attachments
    if ((usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) || 
        (memFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)) {
        return "transient_memoryless";
    }
    // 2. Compute storage write targets (UAVs)
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        return "storage_compute_image";
    }
    // 3. Depth / stencil render targets
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return "depth_stencil_target";
    }
    // 4. Color render targets
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        return "color_render_target";
    }
    // 5. Sampled textures
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        return "static_texture";
    }
    return "general_image";
}

// Construct a Perfetto track name for visualizing memory usage by category in the UI.
static std::string GetUsageTrackName(bool is_driver, std::string_view usage) {
    std::string track = is_driver ? "vulkan.mem.driver.usage." : "vulkan.mem.app.usage.";
    track += usage;
    return track;
}

void DeviceMemoryReport::AddCounterBytes(const std::string& track, uint64_t size) {
    uint64_t& bytes = usage_memory_bytes_[track];
    bytes += size;
    TRACE_COUNTER("VulkanDeviceMemoryReport", GetCounterTrack(track), bytes);
}

void DeviceMemoryReport::SubtractCounterBytes(const std::string& track, uint64_t size) {
    uint64_t& bytes = usage_memory_bytes_[track];
    bytes = (bytes >= size) ? (bytes - size) : 0;
    TRACE_COUNTER("VulkanDeviceMemoryReport", GetCounterTrack(track), bytes);
}

void DeviceMemoryReport::UpdateAllocationUnboundCounter(const AllocationKey& key) {
    auto allocation_it = memory_allocations_.find(key);
    if (allocation_it == memory_allocations_.end()) return;
    auto& allocation = allocation_it->second;

    uint64_t bound_size = 0;
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> intervals;
    intervals.reserve(allocation.sub_allocations.size());
    for (const auto& suballocation : allocation.sub_allocations) {
        // Only the part of a binding that lies within the allocation is bound memory of it; an
        // out-of-range binding must not hide unbound bytes. Clamping here, not at bind time, means
        // bindings recorded against a placeholder (total_size == 0) are counted once the size is known.
        if (suballocation.size > 0 && suballocation.offset < allocation.total_size) {
            VkDeviceSize end = suballocation.offset + std::min(suballocation.size, allocation.total_size - suballocation.offset);
            intervals.emplace_back(suballocation.offset, end);
        }
    }
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());

        // Overlapping and adjacent intervals are merged:
        // bound_size is calculated as the mathematical union (distinct physical footprint) of all intervals.
        VkDeviceSize current_start = intervals[0].first;
        VkDeviceSize current_end = intervals[0].second;
        for (size_t i = 1; i < intervals.size(); ++i) {
            if (intervals[i].first <= current_end) {
                // Overlapping or adjacent interval: extend current merged range
                current_end = std::max(current_end, intervals[i].second);
            } else {
                // Disjoint interval: add previous merged interval size and begin next range
                bound_size += (current_end - current_start);
                current_start = intervals[i].first;
                current_end = intervals[i].second;
            }
        }
        bound_size += (current_end - current_start);
    }

    // Intervals are clamped to the allocation, so bound_size never exceeds total_size.
    uint64_t new_unbound = allocation.total_size - bound_size;
    
    const char* cluster_name = allocation.is_driver ? "driver" : "unbound_memory";
    if (allocation.is_driver &&
        (allocation.object_type == VK_OBJECT_TYPE_IMAGE ||
         allocation.object_type == VK_OBJECT_TYPE_BUFFER)) {
        auto res_it = resources_.find({allocation.object_type, allocation.object_handle});
        // If the memory object has an associated resource of the matching type, use it as the track name.
        if (res_it != resources_.end()) {
            cluster_name = res_it->second.GetCluster(allocation.mem_flags);
        }
    }
    allocation.cluster_name = cluster_name;
    std::string new_unbound_track = GetUsageTrackName(allocation.is_driver, cluster_name);

    // If the unbound memory usage track name or the number of unbound bytes has changed,
    // update the global counters by subtracting the old bytes from the old track 
    // and adding the new bytes to the new track.
    if (allocation.unbound_usage_track != new_unbound_track || allocation.applied_unbound_bytes != new_unbound) {
        if (allocation.applied_unbound_bytes > 0) {
            SubtractCounterBytes(allocation.unbound_usage_track, allocation.applied_unbound_bytes);
        }
        if (new_unbound > 0) {
            AddCounterBytes(new_unbound_track, new_unbound);
        }
    }
    allocation.unbound_usage_track = new_unbound_track;
    allocation.applied_unbound_bytes = new_unbound;
}

namespace {

struct AllocationTraceEvent {
    const char* operation;
    const char* source;
    uint64_t memory_object_id;
    VkDeviceSize size;
    VkDeviceSize offset;
    VkObjectType object_type;
    uint64_t object_handle;
    const char* memory_type;
};

void EmitAllocationTraceEvent(const AllocationTraceEvent& event) {
    TRACE_EVENT_INSTANT("VulkanDeviceMemoryReport", "VulkanMemoryAllocation",
                        "operation", event.operation,
                        "source", event.source,
                        "memory_object_id", event.memory_object_id,
                        "size", static_cast<uint64_t>(event.size),
                        "offset", static_cast<uint64_t>(event.offset),
                        "object_type", static_cast<int32_t>(event.object_type),
                        "object_handle", event.object_handle,
                        "memory_type", event.memory_type);
}

/**
 * @brief Writes one object name to the trace, for consumers to join onto memory events by handle.
 *
 * Emits a "VulkanObjectName" instant event under the "VulkanDeviceMemoryReport" category with
 * three debug annotations:
 *   - "object_type" (int32_t): the VkObjectType value of the named object.
 *   - "object_handle" (uint64_t): the raw Vulkan handle of the object.
 *   - "object_name" (string): the debug name assigned to the object, or an empty string when a
 *     previously assigned name has been cleared.
 */
void EmitDebugObjectName(VkObjectType object_type, uint64_t object_handle, std::string_view name) {
    TRACE_EVENT_INSTANT("VulkanDeviceMemoryReport", "VulkanObjectName",
                        "object_type", static_cast<int32_t>(object_type),
                        "object_handle", object_handle,
                        "object_name", name);
}

}  // namespace

void DeviceMemoryReport::RemoveResourceBinding(const ObjectKey& resource_key, Resource& resource) {
    if (resource.bound_memory == 0) return;

    const uint64_t memory_handle = resource.bound_memory;
    resource.bound_memory = 0;

    auto allocation_it = memory_allocations_.find(AllocationKey::App(memory_handle));
    if (allocation_it == memory_allocations_.end()) return;

    auto& allocation = allocation_it->second;
    const auto& [object_type, object_handle] = resource_key;
    bool is_image = (object_type == VK_OBJECT_TYPE_IMAGE);
    const char* cluster_name = resource.GetCluster(allocation.mem_flags);

    auto& suballocations = allocation.sub_allocations;
    // Search by resource key to identify which specific suballocation to remove,
    // since a single memory block can have multiple resources bound to it.
    for (auto it = suballocations.begin(); it != suballocations.end(); ++it) {
        if (it->object_type == object_type && it->resource_handle == object_handle) {
            VkDeviceSize suballocation_size = it->size;
            VkDeviceSize suballocation_offset = it->offset;
            SubtractCounterBytes(it->usage_track, suballocation_size);
            suballocations.erase(it);
            UpdateAllocationUnboundCounter(AllocationKey::App(memory_handle));

            EmitAllocationTraceEvent({
                .operation = "DESTROY",
                .source = is_image ? "IMAGE" : "BUFFER",
                .memory_object_id = memory_handle,
                .size = suballocation_size,
                .offset = suballocation_offset,
                .object_type = object_type,
                .object_handle = object_handle,
                .memory_type = cluster_name,
            });
            break;
        }
    }
}

void DeviceMemoryReport::BindResourceMemory(VkObjectType object_type, uint64_t resource_handle, uint64_t memory_handle, VkDeviceSize memory_offset) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    const ObjectKey resource_key = {object_type, resource_handle};
    auto res_it = resources_.find(resource_key);
    if (res_it == resources_.end() || res_it->second.size == 0) return;

    // If the same resource handle is passed more than once, remove stale bindings first.
    RemoveResourceBinding(resource_key, res_it->second);

    const AllocationKey key = AllocationKey::App(memory_handle);
    auto& allocation = memory_allocations_[key];
    VkDeviceSize res_size = res_it->second.size;
    std::string new_usage_track = GetUsageTrackName(allocation.is_driver, res_it->second.GetCluster(allocation.mem_flags));

    // Suballocations represent individual resources (like buffers or images) that are bound 
    // to specific offset regions within a single large memory allocation. 
    // We add a record here to track this specific resource's footprint within the larger memory block.
    allocation.sub_allocations.push_back({ object_type, resource_handle, memory_offset, res_size, new_usage_track });
    res_it->second.bound_memory = memory_handle;

    // Each distinct virtual resource handle adds its virtual size to its specific category track upon binding.
    AddCounterBytes(new_usage_track, res_size);
    UpdateAllocationUnboundCounter(key);

    bool is_image = (object_type == VK_OBJECT_TYPE_IMAGE);
    const char* cluster_name = res_it->second.GetCluster(allocation.mem_flags);
    EmitAllocationTraceEvent({
        .operation = "BIND",
        .source = is_image ? "IMAGE" : "BUFFER",
        .memory_object_id = memory_handle,
        .size = res_size,
        .offset = memory_offset,
        .object_type = object_type,
        .object_handle = resource_handle,
        .memory_type = cluster_name,
    });
}

void DeviceMemoryReport::OnBindBufferMemory(uint64_t buffer_handle, uint64_t memory_handle, VkDeviceSize memory_offset) {
    BindResourceMemory(VK_OBJECT_TYPE_BUFFER, buffer_handle, memory_handle, memory_offset);
}

void DeviceMemoryReport::OnBindImageMemory(uint64_t image_handle, uint64_t memory_handle, VkDeviceSize memory_offset) {
    BindResourceMemory(VK_OBJECT_TYPE_IMAGE, image_handle, memory_handle, memory_offset);
}

void DeviceMemoryReport::RemoveAllocationTracking(const AllocationKey& key) {
    auto allocation_it = memory_allocations_.find(key);
    if (allocation_it == memory_allocations_.end()) return;

    auto& allocation = allocation_it->second;
    for (const auto& suballocation : allocation.sub_allocations) {
        SubtractCounterBytes(suballocation.usage_track, suballocation.size);
        auto resource_iterator = resources_.find({suballocation.object_type, suballocation.resource_handle});
        bool is_image = (suballocation.object_type == VK_OBJECT_TYPE_IMAGE);
        const char* cluster_name = "unbound_memory";
        if (resource_iterator != resources_.end()) {
            cluster_name = resource_iterator->second.GetCluster(allocation.mem_flags);
            resource_iterator->second.bound_memory = 0;
        }
        EmitAllocationTraceEvent({
            .operation = "DESTROY",
            .source = is_image ? "IMAGE" : "BUFFER",
            .memory_object_id = key.id,
            .size = suballocation.size,
            .offset = suballocation.offset,
            .object_type = suballocation.object_type,
            .object_handle = suballocation.resource_handle,
            .memory_type = cluster_name,
        });
    }
    if (allocation.applied_unbound_bytes > 0) {
        SubtractCounterBytes(allocation.unbound_usage_track, allocation.applied_unbound_bytes);
    }
    memory_allocations_.erase(allocation_it);
}

void DeviceMemoryReport::OnRecordResourceSize(VkObjectType object_type, uint64_t resource_handle, VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto& res = resources_[{object_type, resource_handle}];
    res.is_image = (object_type == VK_OBJECT_TYPE_IMAGE);
    res.size = size;
}

VkDeviceSize DeviceMemoryReport::GetRecordedResourceSize(VkObjectType object_type, uint64_t resource_handle) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto it = resources_.find({object_type, resource_handle});
    return it != resources_.end() ? it->second.size : 0;
}

uint64_t DeviceMemoryReport::GetUsageCounterBytes(const std::string& track) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto it = usage_memory_bytes_.find(track);
    return it != usage_memory_bytes_.end() ? it->second : 0;
}

void DeviceMemoryReport::Reset() {
    std::scoped_lock lock(map_mutex_, counter_mutex_);
    vk_instance_map_.clear();
    has_callback_map_.clear();
    device_memory_properties_map_.clear();
    resources_.clear();
    memory_allocations_.clear();
    usage_memory_bytes_.clear();
    debug_object_names_.clear();
}

void DeviceMemoryReport::ReattributeDriverAllocations(uint64_t object_handle, VkObjectType object_type) {
    for (auto& pair : memory_allocations_) {
        const AllocationKey& key = pair.first;
        MemoryAllocation& allocation = pair.second;
        if (!key.is_driver || allocation.object_type != object_type || allocation.object_handle != object_handle) {
            continue;
        }
        const char* old_cluster_name = allocation.cluster_name;
        UpdateAllocationUnboundCounter(key);
        if (allocation.total_size == 0 || std::string_view(old_cluster_name) == allocation.cluster_name) {
            continue;
        }
        // Re-attribution is published as DESTROY (old cluster) + CREATE (new cluster) so that
        // consumers summing CREATE/DESTROY sizes per memory_type stay balanced.
        EmitAllocationTraceEvent({
            .operation = "DESTROY",
            .source = "DRIVER",
            .memory_object_id = key.id,
            .size = allocation.total_size,
            .offset = 0,
            .object_type = allocation.object_type,
            .object_handle = allocation.object_handle,
            .memory_type = old_cluster_name,
        });
        EmitAllocationTraceEvent({
            .operation = "CREATE",
            .source = "DRIVER",
            .memory_object_id = key.id,
            .size = allocation.total_size,
            .offset = 0,
            .object_type = allocation.object_type,
            .object_handle = allocation.object_handle,
            .memory_type = allocation.cluster_name,
        });
    }
}

void DeviceMemoryReport::OnCreateImage(uint64_t image_handle, VkImageUsageFlags usage) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto& res = resources_[{VK_OBJECT_TYPE_IMAGE, image_handle}];
    res.is_image = true;
    res.image_usage = usage;
    ReattributeDriverAllocations(image_handle, VK_OBJECT_TYPE_IMAGE);
}

void DeviceMemoryReport::OnCreateBuffer(uint64_t buffer_handle, VkBufferUsageFlags usage, VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto& res = resources_[{VK_OBJECT_TYPE_BUFFER, buffer_handle}];
    res.is_image = false;
    res.buffer_usage = usage;
    res.size = size;
    ReattributeDriverAllocations(buffer_handle, VK_OBJECT_TYPE_BUFFER);
}

void DeviceMemoryReport::OnDestroyObject(uint64_t object_handle, VkObjectType object_type) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto res_it = resources_.find({object_type, object_handle});
    if (res_it != resources_.end()) {
        RemoveResourceBinding(res_it->first, res_it->second);
        resources_.erase(res_it);
    }
    if (debug_object_names_.erase(std::make_pair(object_type, object_handle)) > 0) {
        EmitDebugObjectName(object_type, object_handle, "");
    }
}

void DeviceMemoryReport::SetDebugObjectName(VkObjectType object_type, uint64_t object_handle, const char* name) {
    // Other types would be trace volume that nothing reads; VK_LAYER_GOOGLE_DebugMarker names them
    // all for consumers that need it.
    switch (object_type) {
        case VK_OBJECT_TYPE_BUFFER:
        case VK_OBJECT_TYPE_IMAGE:
        case VK_OBJECT_TYPE_DEVICE_MEMORY:
            break;
        default:
            return;
    }

    // A null or empty name clears the name, and the clear still has to be published. A view, not a
    // string: the overwhelmingly common call is an application re-applying a name that has not
    // changed, some do it every frame, and that path must not allocate.
    const std::string_view new_name = name ? std::string_view(name) : std::string_view();
    const auto key = std::make_pair(object_type, object_handle);

    std::lock_guard<std::mutex> lock(counter_mutex_);

    auto existing = debug_object_names_.find(key);
    if (existing == debug_object_names_.end()) {
        // Nothing stored and nothing to store: no state change to publish.
        if (new_name.empty()) return;
        debug_object_names_.emplace(key, new_name);
    } else if (existing->second == new_name) {
        // Applications re-apply the same name routinely, so only a change is worth publishing.
        return;
    } else if (new_name.empty()) {
        debug_object_names_.erase(existing);
    } else {
        // Assigning in place reuses the capacity already allocated for the previous name.
        existing->second.assign(new_name);
    }
    EmitDebugObjectName(object_type, object_handle, new_name);
}

void DeviceMemoryReport::SetDebugObjectName(VkDebugReportObjectTypeEXT object_type, uint64_t object_handle, const char* name) {
    // VK_EXT_debug_marker names objects with the legacy VkDebugReportObjectTypeEXT enum. Only the
    // types this layer attributes memory to are mapped.
    VkObjectType mapped_type = VK_OBJECT_TYPE_UNKNOWN;
    switch (object_type) {
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT:
            mapped_type = VK_OBJECT_TYPE_BUFFER;
            break;
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT:
            mapped_type = VK_OBJECT_TYPE_IMAGE;
            break;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT:
            mapped_type = VK_OBJECT_TYPE_DEVICE_MEMORY;
            break;
        default:
            return;
    }
    SetDebugObjectName(mapped_type, object_handle, name);
}

void DeviceMemoryReport::EmitAllDebugObjectNames() {
    for (const auto& [key, name] : debug_object_names_) {
        const auto& [object_type, object_handle] = key;
        EmitDebugObjectName(object_type, object_handle, name);
    }
}

void DeviceMemoryReport::DumpCurrentCountersAndAllocations() {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    for (const auto& pair : usage_memory_bytes_) {
        if (pair.second > 0) {
            TRACE_COUNTER("VulkanDeviceMemoryReport", GetCounterTrack(pair.first), pair.second);
        }
    }

    for (const auto& pair : memory_allocations_) {
        uint64_t memory_handle = pair.first.id;
        const auto& allocation = pair.second;
        if (allocation.total_size == 0) continue;

        const char* memory_type = allocation.is_driver ? allocation.cluster_name : "unbound_memory";
        EmitAllocationTraceEvent({
            .operation = "CREATE",
            .source = allocation.is_driver ? "DRIVER" : "DEVICE_MEMORY",
            .memory_object_id = memory_handle,
            .size = allocation.total_size,
            .offset = 0,
            .object_type = allocation.object_type,
            .object_handle = allocation.object_handle,
            .memory_type = memory_type,
        });

        for (const auto& suballocation : allocation.sub_allocations) {
            auto resource_iterator = resources_.find({suballocation.object_type, suballocation.resource_handle});
            bool is_image = (suballocation.object_type == VK_OBJECT_TYPE_IMAGE);
            const char* cluster_name = (resource_iterator != resources_.end())
                                           ? resource_iterator->second.GetCluster(allocation.mem_flags)
                                           : "unbound_memory";

            EmitAllocationTraceEvent({
                .operation = "BIND",
                .source = is_image ? "IMAGE" : "BUFFER",
                .memory_object_id = memory_handle,
                .size = suballocation.size,
                .offset = suballocation.offset,
                .object_type = suballocation.object_type,
                .object_handle = suballocation.resource_handle,
                .memory_type = cluster_name,
            });
        }
    }

    // Names are replayed after the allocations for the same reason they are replayed at all: a
    // session that attaches mid-run never saw the naming calls, and a name with no allocation to
    // attach to is meaningless.
    EmitAllDebugObjectNames();
}

void DeviceMemoryReport::OnMemoryReportEvent(const VkDeviceMemoryReportCallbackDataEXT* pCallbackData) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    // Per the VK_EXT_device_memory_report specification, VkDeviceMemoryReportFlagsEXT is reserved and always 0.
    // Explicit application allocations (vkAllocateMemory / memory imports) report VK_OBJECT_TYPE_DEVICE_MEMORY
    // with objectHandle set to the VkDeviceMemory handle, which we use as the key for compatibility with Bind*Memory.
    // Driver-internal allocations report other objectTypes (or VK_OBJECT_TYPE_UNKNOWN) and a single object
    // (e.g. VkImage) may have multiple distinct memory allocations, so we use memoryObjectId as the key.
    const bool is_driver = pCallbackData->objectType != VK_OBJECT_TYPE_DEVICE_MEMORY;
    const AllocationKey key = is_driver ? AllocationKey::Driver(pCallbackData->memoryObjectId)
                                        : AllocationKey::App(pCallbackData->objectHandle);
    const char* source = is_driver ? "DRIVER" : "DEVICE_MEMORY";

    const char* operation_name = nullptr;
    const char* memory_type = "unbound_memory";
    VkDeviceSize event_size = pCallbackData->size;
    VkObjectType event_object_type = pCallbackData->objectType;
    uint64_t event_object_handle = pCallbackData->objectHandle;
    if (pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT ||
        pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT) {
        // An entry with total_size == 0 is a placeholder created ahead of the allocation (e.g. by
        // SetAllocationMemoryProperties or BindResourceMemory) and must be kept so its mem_flags and
        // bindings carry over. An entry with total_size > 0 is a live allocation for which no
        // FREE/UNIMPORT arrived; retire it (counters and trace) so its bytes do not leak.
        auto existing_it = memory_allocations_.find(key);
        if (existing_it != memory_allocations_.end() && existing_it->second.total_size > 0) {
            const MemoryAllocation& stale = existing_it->second;
            const char* stale_memory_type = is_driver ? stale.cluster_name : "unbound_memory";
            const VkDeviceSize stale_size = stale.total_size;
            const VkObjectType stale_object_type = stale.object_type;
            const uint64_t stale_object_handle = stale.object_handle;
            RemoveAllocationTracking(key);
            EmitAllocationTraceEvent({
                .operation = "DESTROY",
                .source = source,
                .memory_object_id = key.id,
                .size = stale_size,
                .offset = 0,
                .object_type = stale_object_type,
                .object_handle = stale_object_handle,
                .memory_type = stale_memory_type,
            });
            if (!is_driver &&
                debug_object_names_.erase(std::make_pair(VK_OBJECT_TYPE_DEVICE_MEMORY, key.id)) > 0) {
                EmitDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, key.id, "");
            }
        }
        auto& allocation = memory_allocations_[key];
        allocation.total_size = pCallbackData->size;
        allocation.is_driver = is_driver;
        allocation.object_type = pCallbackData->objectType;
        allocation.object_handle = pCallbackData->objectHandle;
        UpdateAllocationUnboundCounter(key);
        operation_name = "CREATE";
        memory_type = is_driver ? allocation.cluster_name : "unbound_memory";
    } else if (pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT ||
               pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT) {
        auto allocation_iterator = memory_allocations_.find(key);
        if (allocation_iterator != memory_allocations_.end()) {
            memory_type = is_driver ? allocation_iterator->second.cluster_name : "unbound_memory";
            event_size = allocation_iterator->second.total_size;
            event_object_type = allocation_iterator->second.object_type;
            event_object_handle = allocation_iterator->second.object_handle;
            RemoveAllocationTracking(key);
            operation_name = "DESTROY";
        }
    }

    if (operation_name != nullptr) {
        EmitAllocationTraceEvent({
            .operation = operation_name,
            .source = source,
            .memory_object_id = key.id,
            .size = event_size,
            .offset = 0,
            .object_type = event_object_type,
            .object_handle = event_object_handle,
            .memory_type = memory_type,
        });
    }

    const bool is_free = pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT ||
                         pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT;
    if (is_free && !is_driver &&
        debug_object_names_.erase(std::make_pair(VK_OBJECT_TYPE_DEVICE_MEMORY, key.id)) > 0) {
        EmitDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, key.id, "");
    }
}

void DeviceMemoryReport::SetHasMemoryReportCallback(VkDevice device, bool has_callback) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    has_callback_map_[device] = has_callback;
}

void DeviceMemoryReport::OnAllocateMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, uint32_t memory_type_index, VkMemoryPropertyFlags property_flags) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    uint64_t handle = reinterpret_cast<uint64_t>(memory);
    const AllocationKey key = AllocationKey::App(handle);
    auto& allocation = memory_allocations_[key];

    VkMemoryPropertyFlags mem_flags = property_flags;
    if (mem_flags == 0 && memory_type_index != UINT32_MAX) {
        auto prop_it = device_memory_properties_map_.find(device);
        if (prop_it != device_memory_properties_map_.end() && memory_type_index < prop_it->second.memoryTypeCount) {
            mem_flags = prop_it->second.memoryTypes[memory_type_index].propertyFlags;
        }
    }
    bool mem_flags_changed = (allocation.mem_flags != mem_flags);
    allocation.mem_flags = mem_flags;

    if (has_callback_map_[device]) {
        if (mem_flags_changed && allocation.total_size > 0) {
            UpdateAllocationUnboundCounter(key);
        }
    } else {
        allocation.total_size = size;
        allocation.is_driver = false;
        allocation.object_type = VK_OBJECT_TYPE_DEVICE_MEMORY;
        allocation.object_handle = handle;
        UpdateAllocationUnboundCounter(key);

        EmitAllocationTraceEvent({
            .operation = "CREATE",
            .source = "DEVICE_MEMORY",
            .memory_object_id = handle,
            .size = size,
            .offset = 0,
            .object_type = VK_OBJECT_TYPE_DEVICE_MEMORY,
            .object_handle = handle,
            .memory_type = "unbound_memory",
        });
    }
}

void DeviceMemoryReport::OnFreeMemory(VkDevice device, VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    if (has_callback_map_[device]) return;

    uint64_t handle = reinterpret_cast<uint64_t>(memory);
    const AllocationKey key = AllocationKey::App(handle);
    auto allocation_iterator = memory_allocations_.find(key);
    if (allocation_iterator != memory_allocations_.end()) {
        VkDeviceSize freed_size = allocation_iterator->second.total_size;
        RemoveAllocationTracking(key);

        EmitAllocationTraceEvent({
            .operation = "DESTROY",
            .source = "DEVICE_MEMORY",
            .memory_object_id = handle,
            .size = freed_size,
            .offset = 0,
            .object_type = VK_OBJECT_TYPE_DEVICE_MEMORY,
            .object_handle = handle,
            .memory_type = "unbound_memory",
        });
    }
    if (debug_object_names_.erase(std::make_pair(VK_OBJECT_TYPE_DEVICE_MEMORY, handle)) > 0) {
        EmitDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, handle, "");
    }
}
