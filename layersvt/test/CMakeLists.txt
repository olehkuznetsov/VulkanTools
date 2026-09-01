# ~~~
# Copyright (c) 2023-2025 Valve Corporation
# Copyright (c) 2023-2025 LunarG, Inc.
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
# ~~~
if (ANDROID)
    return()
endif()

if (NOT MSVC)
    set_source_files_properties(../perfetto/perfetto.cc PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
endif()

function(LayerTest NAME)
    string(TOLOWER ${NAME} LOWER_NAME)
    set(TEST_FILENAME ./test_${LOWER_NAME}.cpp)
    set(TEST_NAME test_${NAME}_layer)

    file(GLOB TEST_JSON_FILES ${CMAKE_SOURCE_DIR}/profiles/test/data/)

    add_executable(${TEST_NAME}
                   ${TEST_FILENAME}
                   layer_test_helper.h
                   layer_test_helper.cpp
                   layer_test_main.cpp
                   layer_test_framework.cpp
                   layer_test_framework.h)
    add_dependencies(${TEST_NAME} VkLayer_${NAME})
    target_link_libraries(${TEST_NAME} Vulkan::Headers Vulkan::Loader GTest::gtest GTest::gtest_main Vulkan::LayerSettings)
    if (${NAME} STREQUAL "DebugMarker")
        target_sources(${TEST_NAME} PRIVATE ../debug_marker/debug_marker.cpp ../debug_marker/debug_marker_perfetto.cpp ../perfetto/perfetto.cc)
        target_include_directories(${TEST_NAME} PRIVATE .. ../debug_marker)
    elseif (${NAME} STREQUAL "DeviceMemoryReport")
        target_sources(${TEST_NAME} PRIVATE ../device_memory_report/device_memory_report.cpp ../device_memory_report/device_memory_report_perfetto.cpp ../perfetto/perfetto.cc)
        target_include_directories(${TEST_NAME} PRIVATE .. ../device_memory_report)
    endif()
    target_compile_definitions(${TEST_NAME} PUBLIC LAYER_BINARY_PATH="$<TARGET_FILE_DIR:VkLayer_${NAME}>")
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    set_target_properties(${TEST_NAME} PROPERTIES FOLDER "layers/${NAME}/Test")

    if(WIN32 AND (QT_TARGET_TYPE STREQUAL STATIC_LIBRARY))
      set_property(TARGET ${TEST_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endfunction()

set(LAYER_TEST_FILES api_dump monitor screenshot CPUTiming DebugMarker DeviceMemoryReport)

foreach(test_item ${LAYER_TEST_FILES})
    # If the target doesn't exist continue.
    if (NOT TARGET "VkLayer_${test_item}")
        continue()
    endif()

    LayerTest(${test_item})
endforeach()

add_executable(test_common_layer
    test_common.cpp
    layer_test_main.cpp
)
target_link_libraries(test_common_layer PRIVATE
    layersvt_common
    GTest::gtest
    Vulkan::Headers
    Vulkan::UtilityHeaders
)
add_test(NAME test_common_layer COMMAND test_common_layer)
set_target_properties(test_common_layer PROPERTIES FOLDER "layers/common/Test")

