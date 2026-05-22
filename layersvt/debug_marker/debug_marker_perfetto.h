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
 
#ifndef LAYERSVT_DEBUG_MARKER_PERFETTO_H
#define LAYERSVT_DEBUG_MARKER_PERFETTO_H

#include "perfetto/perfetto.h"

// Define categories used for Perfetto tracing in DebugMarker layer.
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("VulkanDebugMarker").SetDescription("Vulkan Debug Markers and Labels")
);

void InitializeDebugMarkerPerfetto();

#endif // LAYERSVT_DEBUG_MARKER_PERFETTO_H
