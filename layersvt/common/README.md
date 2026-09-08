# VulkanTools Common Layer Foundation (`layersvt_common`)

The `layersvt_common` library provides a modern, thread-safe C++ foundation for developing Vulkan layers in the `VulkanTools` repository. It eliminates repetitive Vulkan loader dispatch boilerplate, centralizes dispatch table and device lifecycle management, and guarantees standard loader compliance across Android, Linux, and Windows.

---

## 1. Architectural Overview

```
                          +-----------------------------------+
                          |     Vulkan Loader / Application   |
                          +-----------------------------------+
                                            |
                                            v
                          +-----------------------------------+
                          |       layersvt_entrypoints        |
                          |  (Exported C ABI vkGet*ProcAddr)  |
                          +-----------------------------------+
                                            |
                                            v
                          +-----------------------------------+
                          |        LayerBase (Singleton)      |
                          |   Template Method Dispatch Engine |
                          +-----------------------------------+
                                         /     \
                                        v       v
                          +---------------+  +--------------------+
                          | LayerManifest |  | DispatchTable-     |
                          | Declarative   |  | Manager            |
                          | Metadata      |  | Dispatch Tables &  |
                          | & Extensions  |  | Physical Device    |
                          |               |  | Tracking           |
                          +---------------+  +--------------------+
                                                |
                                                v
                          +-----------------------------------+
                          |        dispatch_downstream.h      |
                          | (Template Metaprogrammed Forward) |
                          +-----------------------------------+
                                             |
                                             v
                          +-----------------------------------+
                          |     Next Layer / Vulkan Driver    |
                          +-----------------------------------+
```

### Key Components

* **`LayerBase`** ([`layer_base.h`](layer_base.h), [`layer_base.cpp`](layer_base.cpp)):
  Base class employing the **Template Method** design pattern. It implements static intercept entry points (`vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateInstance`, `vkDestroyInstance`, `vkCreateDevice`, `vkDestroyDevice`, `vkEnumeratePhysicalDevices`, `vkEnumeratePhysicalDeviceGroups`, `vkEnumerateInstanceExtensionProperties`, `vkEnumerateDeviceExtensionProperties`, `vkGetPhysicalDeviceToolProperties`), executes virtual lifecycle and extension/tooling hooks (`ProcessDeviceExtensions`, `ProcessInstanceExtensions`, `ProcessToolProperties`), and delegates custom functions to derived layer overrides.
* **`LayerManifest`** ([`layer_manifest.h`](layer_manifest.h), [`layer_manifest.cpp`](layer_manifest.cpp)):
  Passive declarative data struct describing layer metadata, supported Vulkan versions, advertised instance/device extensions, and tooling properties (`VK_EXT_tooling_info` / `VK_VERSION_1_3`). Downstream Vulkan querying, buffer sizing, and capability merging are managed by `LayerBase`.
* **`DispatchTableManager`** ([`dispatch_table_manager.h`](dispatch_table_manager.h), [`dispatch_table_manager.cpp`](dispatch_table_manager.cpp)):
  Thread-safe registry for `VkuInstanceDispatchTable` and `VkuDeviceDispatchTable` keyed by dispatchable handle. Incorporates native `VkPhysicalDevice` to parent `VkInstance` tracking and single-lock atomic teardown during instance destruction. Tracks and forwards `VK_LOADER_DATA_CALLBACK` to initialize dispatchable handles created internally by layers.
* **`dispatch_downstream.h`** ([`dispatch_downstream.h`](dispatch_downstream.h)):
  Header-only template metaprogramming helpers (`DispatchDownstream`, `DispatchDownstreamOr`, `DispatchDownstreamOrSuccess`) that deduce table types at compile time and forward commands downstream.
* **`layersvt_entrypoints`** ([`layer_entrypoints.cpp`](layer_entrypoints.cpp)):
  CMake `OBJECT` library that exports standard C symbols (`vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkNegotiateLoaderLayerInterfaceVersion`, and the four Android loader enumeration entry points) without macro duplication.

---

## 2. Step-by-Step: Adding a New Layer

Follow this 5-step guide to add a new layer (e.g. `MyCustomLayer`).

### Step 1: Declare the Layer Class (`my_custom_layer.h`)

Inherit from `layersvt::LayerBase`. Shadow `LayerBase::Get()` to return your derived layer instance, and override only the lifecycle hooks and command intercepts your layer needs:

```cpp
#pragma once

#include <common/layer_base.h>
#include <common/layer_manifest.h>

namespace layersvt {

class MyCustomLayer : public LayerBase {
 public:
  static MyCustomLayer& Get();

 protected:
  // Return declarative manifest
  const LayerManifest* GetLayerManifest() const override;

  // Intercept custom or extension commands
  PFN_vkVoidFunction GetLayerInstanceCommand(const char* command_name) override;
  PFN_vkVoidFunction GetLayerDeviceCommand(const char* command_name) override;

  // Lifecycle hooks (override as needed)
  void PreCreateInstance(VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator) override;
  void PostCreateInstance(VkInstance instance, const VkInstanceCreateInfo* create_info,
                          const VkAllocationCallbacks* allocator) override;
  void PreDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) override;

  void PreCreateDevice(VkPhysicalDevice physical_device, VkDeviceCreateInfo* create_info,
                       const VkAllocationCallbacks* allocator) override;
  void PostCreateDevice(VkDevice device, VkPhysicalDevice physical_device,
                        const VkDeviceCreateInfo* create_info,
                        const VkAllocationCallbacks* allocator) override;
  void PreDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) override;
};

}  // namespace layersvt
```

### Step 2: Implement the Layer (`my_custom_layer.cpp`)

Implement the layer methods, configure the static manifest, and implement custom intercepted commands:

```cpp
#include "my_custom_layer.h"
#include <common/dispatch_downstream.h>

#include <cstring>

namespace layersvt {

namespace {

// 1. Static layer instance (instantiated in the shared library entrypoint, not in files linked to unit tests)
MyCustomLayer g_layer;

// 2. Declarative layer manifest
const LayerManifest kManifest({
    .layer_name = "VK_LAYER_GOOGLE_MyCustomLayer",
    .description = "Google Vulkan MyCustomLayer",
    .spec_version = VK_API_VERSION_1_3,
    .implementation_version = 1,
    .instance_extensions = {},
    .device_extensions = {
        VkExtensionProperties{"VK_EXT_custom_extension", 1},
        VkExtensionProperties{VK_EXT_TOOLING_INFO_EXTENSION_NAME, VK_EXT_TOOLING_INFO_SPEC_VERSION},
    },
    .tool_properties = VkPhysicalDeviceToolPropertiesEXT{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
        .pNext = nullptr,
        .name = "MyCustomLayer",
        .version = "1.0",
        .purposes = VK_TOOL_PURPOSE_PROFILING_BIT_EXT,
        .description = "Google Vulkan MyCustomLayer",
        .layer = "VK_LAYER_GOOGLE_MyCustomLayer",
    },
});

// Custom intercepted Vulkan command
VKAPI_ATTR void VKAPI_CALL Hook_vkCmdDraw(VkCommandBuffer command_buffer, uint32_t vertex_count,
                                          uint32_t instance_count, uint32_t first_vertex,
                                          uint32_t first_instance) {
    // Custom layer logic before dispatch ...

    // Forward downstream to the next layer/driver
    layersvt::DispatchDownstream<&VkuDeviceDispatchTable::CmdDraw>(
        command_buffer, vertex_count, instance_count, first_vertex, first_instance);

    // Custom layer logic after dispatch ...
}

}  // namespace

MyCustomLayer& MyCustomLayer::Get() {
    assert(LayerBase::Get() != nullptr);
    return *static_cast<MyCustomLayer*>(LayerBase::Get());
}

const LayerManifest* MyCustomLayer::GetLayerManifest() const {
    return &kManifest;
}

PFN_vkVoidFunction MyCustomLayer::GetLayerDeviceCommand(const char* command_name) {
    assert(command_name != nullptr);
    if (std::strcmp(command_name, "vkCmdDraw") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(Hook_vkCmdDraw);
    }
    return nullptr;
}

PFN_vkVoidFunction MyCustomLayer::GetLayerInstanceCommand(const char* /*command_name*/) {
    return nullptr;
}

void MyCustomLayer::PostCreateDevice(VkDevice device, VkPhysicalDevice physical_device,
                                     const VkDeviceCreateInfo* /*create_info*/,
                                     const VkAllocationCallbacks* /*allocator*/) {
    // Setup per-device state ...
}

void MyCustomLayer::PreDestroyDevice(VkDevice device, const VkAllocationCallbacks* /*allocator*/) {
    // Teardown per-device state before downstream destruction ...
}

}  // namespace layersvt
```

### Step 3: Configure CMake Target (`layersvt/CMakeLists.txt`)

Add the shared library module target, linking `layersvt_common` and including `$<TARGET_OBJECTS:layersvt_entrypoints>`:

```cmake
add_library(VkLayer_MyCustomLayer MODULE
    my_custom_layer/my_custom_layer.cpp
    my_custom_layer/my_custom_layer.h
    $<TARGET_OBJECTS:layersvt_entrypoints>
)

target_include_directories(VkLayer_MyCustomLayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(VkLayer_MyCustomLayer PRIVATE
    layersvt_common
    Vulkan::Headers
    Vulkan::LayerSettings
)

# Configure output filename and definition file on Windows
if (WIN32)
    target_sources(VkLayer_MyCustomLayer PRIVATE my_custom_layer/VkLayer_MyCustomLayer.def)
endif()

list(APPEND TOOL_LAYERS "VkLayer_MyCustomLayer")
```

### Step 4: JSON Manifest and Windows DEF

1. **`my_custom_layer/VkLayer_MyCustomLayer.json.in`**:
   Standard Vulkan layer manifest template configured by CMake:
   ```json
   {
       "file_format_version": "1.2.0",
       "layer": {
           "name": "VK_LAYER_GOOGLE_MyCustomLayer",
           "type": "GLOBAL",
           "library_path": "@JSON_LIBRARY_PATH@",
           "api_version": "1.3.0",
           "implementation_version": "1",
           "description": "Google Vulkan MyCustomLayer"
       }
   }
   ```
2. **`my_custom_layer/VkLayer_MyCustomLayer.def`** (Windows):
   Export entry point symbols:
   ```def
   LIBRARY VkLayer_MyCustomLayer
   EXPORTS
       vkGetInstanceProcAddr
       vkGetDeviceProcAddr
       vkNegotiateLoaderLayerInterfaceVersion
       vkEnumerateInstanceExtensionProperties
       vkEnumerateInstanceLayerProperties
       vkEnumerateDeviceExtensionProperties
       vkEnumerateDeviceLayerProperties
   ```

### Step 5: Add Unit Tests (`layersvt/test/test_mycustomlayer.cpp`)

Write unit tests using GoogleTest and the test framework. To keep tests hermetic and restore clean state across fixtures, reset the global layer instance:

```cpp
#include <gtest/gtest.h>
#include "common/layer_base_test_peer.h"
#include "layer_test_helper.h"
#include "my_custom_layer/my_custom_layer.h"

namespace layersvt {

class MyCustomLayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
      // Re-create layer to restore pristine state
      // (destructor cleans up tracker and dispatch table entries)
      layer_test::ResetLayer<MyCustomLayer>();
  }
};

TEST_F(MyCustomLayerTest, ManifestValidation) {
    const LayerManifest* manifest = LayerBaseTestPeer::GetLayerManifest(MyCustomLayer::Get());
    ASSERT_NE(manifest, nullptr);
    EXPECT_STREQ(manifest->layer_name, "VK_LAYER_GOOGLE_MyCustomLayer");
}

}  // namespace layersvt
```

Register the test in `layersvt/test/CMakeLists.txt`:
```cmake
LayerTest(MyCustomLayer)
target_sources(test_MyCustomLayer_layer PRIVATE ../my_custom_layer/my_custom_layer.cpp)
target_link_libraries(test_MyCustomLayer_layer layersvt_common)
```
> **Note**:
> - Do **not** link `layersvt_entrypoints` into test executables that link `Vulkan::Loader`; tests link directly to `layersvt_common` and invoke `LayerBase` APIs via `LayerBaseTestPeer` to avoid symbol collisions with `Vulkan::Loader`.

---

## 3. Best Practices & Conventions

1. **Do Not Reimplement Common Dispatch Boilerplate**:
   Never manually parse `VkLayerInstanceCreateInfo` / `VkLayerDeviceCreateInfo` link chains or allocate raw dispatch tables. `LayerBase` automatically unwraps loader chains, initializes `DispatchTableManager`, and tracks physical devices.
2. **Use `DispatchDownstream` for Downstream Forwarding**:
   Use `DispatchDownstream<&VkuDeviceDispatchTable::CmdDraw>(...)` to invoke the next layer or driver. It automatically deduces instance vs. device dispatch tables from the member pointer at compile time and asserts that tables and function pointers are non-null.
3. **Null Handle Destruction is Safe**:
   `LayerBase::DestroyInstance` and `LayerBase::DestroyDevice` immediately return on `VK_NULL_HANDLE` per Vulkan specification (Section 2.7), bypassing downstream dispatch and virtual hooks. Virtual `PreDestroy*` hooks are guaranteed to receive only valid non-null handles.
4. **Tool Properties Downstream Initialization**:
   When querying downstream tooling properties in custom commands, always initialize `tool.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT` and `tool.pNext = nullptr` on each array element before passing buffers to downstream functions to satisfy Vulkan VUIDs.
5. **Thread Safety**:
   `DispatchTableManager` is fully synchronized via mutexes. Layer-specific global states must similarly protect their own internal maps. Avoid recursive locks across downstream dispatch invocations.
