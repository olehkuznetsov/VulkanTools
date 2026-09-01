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

#include "device_memory_report_perfetto.h"
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

void InitializeDeviceMemoryReportPerfetto() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        perfetto::TracingInitArgs args;
        args.backends = perfetto::kSystemBackend;
        perfetto::Tracing::Initialize(args);
        perfetto::TrackEvent::Register();
    });
}

namespace {
static const std::unordered_map<std::string_view, perfetto::CounterTrack> kStaticTracks = {
    // App usage tracks
    {"vulkan.mem.app.usage.staging_transfer", perfetto::CounterTrack("vulkan.mem.app.usage.staging_transfer").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.ray_tracing", perfetto::CounterTrack("vulkan.mem.app.usage.ray_tracing").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.indirect_gpu_driven", perfetto::CounterTrack("vulkan.mem.app.usage.indirect_gpu_driven").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.compute_storage", perfetto::CounterTrack("vulkan.mem.app.usage.compute_storage").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.uniform_constants", perfetto::CounterTrack("vulkan.mem.app.usage.uniform_constants").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.geometry_mesh", perfetto::CounterTrack("vulkan.mem.app.usage.geometry_mesh").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.transform_feedback", perfetto::CounterTrack("vulkan.mem.app.usage.transform_feedback").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.general_buffer", perfetto::CounterTrack("vulkan.mem.app.usage.general_buffer").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.transient_memoryless", perfetto::CounterTrack("vulkan.mem.app.usage.transient_memoryless").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.storage_compute_image", perfetto::CounterTrack("vulkan.mem.app.usage.storage_compute_image").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.depth_stencil_target", perfetto::CounterTrack("vulkan.mem.app.usage.depth_stencil_target").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.color_render_target", perfetto::CounterTrack("vulkan.mem.app.usage.color_render_target").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.static_texture", perfetto::CounterTrack("vulkan.mem.app.usage.static_texture").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.general_image", perfetto::CounterTrack("vulkan.mem.app.usage.general_image").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.app.usage.unbound_memory", perfetto::CounterTrack("vulkan.mem.app.usage.unbound_memory").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},

    // Driver usage tracks
    {"vulkan.mem.driver.usage.staging_transfer", perfetto::CounterTrack("vulkan.mem.driver.usage.staging_transfer").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.ray_tracing", perfetto::CounterTrack("vulkan.mem.driver.usage.ray_tracing").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.indirect_gpu_driven", perfetto::CounterTrack("vulkan.mem.driver.usage.indirect_gpu_driven").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.compute_storage", perfetto::CounterTrack("vulkan.mem.driver.usage.compute_storage").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.uniform_constants", perfetto::CounterTrack("vulkan.mem.driver.usage.uniform_constants").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.geometry_mesh", perfetto::CounterTrack("vulkan.mem.driver.usage.geometry_mesh").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.transform_feedback", perfetto::CounterTrack("vulkan.mem.driver.usage.transform_feedback").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.general_buffer", perfetto::CounterTrack("vulkan.mem.driver.usage.general_buffer").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.transient_memoryless", perfetto::CounterTrack("vulkan.mem.driver.usage.transient_memoryless").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.storage_compute_image", perfetto::CounterTrack("vulkan.mem.driver.usage.storage_compute_image").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.depth_stencil_target", perfetto::CounterTrack("vulkan.mem.driver.usage.depth_stencil_target").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.color_render_target", perfetto::CounterTrack("vulkan.mem.driver.usage.color_render_target").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.static_texture", perfetto::CounterTrack("vulkan.mem.driver.usage.static_texture").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.general_image", perfetto::CounterTrack("vulkan.mem.driver.usage.general_image").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
    {"vulkan.mem.driver.usage.unbound_memory", perfetto::CounterTrack("vulkan.mem.driver.usage.unbound_memory").set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES)},
};
}  // namespace

perfetto::CounterTrack GetCounterTrack(std::string_view name) {
    auto it = kStaticTracks.find(name);
    if (it != kStaticTracks.end()) {
        return it->second;
    }
    static std::mutex dynamic_mutex;
    static std::unordered_map<std::string, perfetto::CounterTrack> dynamic_tracks;
    static std::unordered_set<std::string> dynamic_names;
    std::lock_guard<std::mutex> lock(dynamic_mutex);
    auto dyn_it = dynamic_tracks.find(std::string(name));
    if (dyn_it != dynamic_tracks.end()) {
        return dyn_it->second;
    }
    auto name_it = dynamic_names.insert(std::string(name)).first;
    perfetto::CounterTrack track = perfetto::CounterTrack(perfetto::DynamicString(name_it->c_str()))
        .set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES);
    dynamic_tracks.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(*name_it),
        std::forward_as_tuple(track));
    return track;
}
