# Copyright (C) 2026 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(layersvt_common OBJECT
    dispatch_table_manager.h
    dispatch_table_manager.cpp
    layer_manifest.h
    layer_manifest.cpp
    layer_base.h
    layer_base.cpp
    dispatch_downstream.h
)

set_target_properties(layersvt_common PROPERTIES
    FOLDER "layers/common"
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(layersvt_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(layersvt_common PUBLIC
    Vulkan::Headers
    Vulkan::UtilityHeaders
)
if (ANDROID)
    target_sources(layersvt_common PRIVATE
        layer_keep_alive.cpp
    )
    target_link_libraries(layersvt_common PUBLIC ${CMAKE_DL_LIBS})
endif()
