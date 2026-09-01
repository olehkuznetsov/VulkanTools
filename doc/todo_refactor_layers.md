# Refactoring VulkanTools Layers: Common Layer Support Library (`layersvt_common`)

## 1. Executive Summary & Scope

The `layersvt` directory contains six Vulkan layers. Based on architectural requirements and maintenance priorities, the refactoring targets the active profiling/tracing layers and explicitly excludes legacy/specialized layers:

### In-Scope Layers (Target for Refactoring):
1. **`VkLayer_DebugMarker`** (`VK_LAYER_GOOGLE_DebugMarker`) &mdash; Intercepts `VK_EXT_debug_marker` and `VK_EXT_debug_utils` object names and labels, emitting them to Perfetto traces (supports late attach).
2. **`VkLayer_CPUTiming`** (`VK_LAYER_GOOGLE_CPUTiming`) &mdash; Measures API call duration and emits Perfetto track events with category filtering.
3. **`VkLayer_DeviceMemoryReport`** (`VK_LAYER_GOOGLE_DeviceMemoryReport`) &mdash; Intercepts `VK_EXT_device_memory_report` and tracks memory allocations by usage type (buffers, textures, surfaces), emitting Perfetto counter tracks (`vulkan.mem.app.usage.*`, `vulkan.mem.driver.usage.*`).
4. **`VkLayer_screenshot`** (`VK_LAYER_LUNARG_screenshot`) &mdash; Captures swapchain frames to image files (PPM/PAM) or Perfetto traces.

### Excluded from Refactoring (Preserved As-Is):
* **`VkLayer_monitor`** (`VK_LAYER_LUNARG_monitor`) &mdash; **Excluded**: A legacy desktop utility layer that appends real-time FPS to OS window title bars on Windows (Win32) and Linux (XCB). It is non-functional on Android/Wayland/macOS, has no active feature development, and is kept as-is to avoid touching legacy windowing code.
* **`VkLayer_api_dump`** (`VK_LAYER_LUNARG_api_dump`) &mdash; **Excluded**: An enormous, heavily code-generated layer (~75k LOC in `api_dump.h` + generator) with C++ template format specialization (`Text`, `Html`, `Json`) and recursive self-introspection (dumping `vkCreateInstance`/`vkCreateDevice` arguments). Forcing `api_dump` into a common base would add unwanted complexity to `layersvt_common` with zero benefit to the other layers.

### Problem Statement
The four in-scope layers (`DebugMarker`, `CPUTiming`, `DeviceMemoryReport`, `screenshot`) evolved independently, resulting in:
- **Code Duplication**: Each layer re-implements Vulkan loader chain traversal, dispatch table management, `VkPhysicalDevice` &rarr; `VkInstance` mapping, layer/extension property enumeration, and Perfetto initialization.
- **Thread Safety Risks**: Core dispatch tables (`vk_layer_table.cpp`) and data maps lack mutex synchronization for concurrent device/command buffer creation.
- **Maintenance Overhead**: Workarounds (such as Android b/143293104 extension enumeration or `RTLD_NODELETE` keep-alive) are implemented in only a subset of layers, leaving others vulnerable to known platform issues.
- **Build Time Overhead**: The ~100k-line monolithic `perfetto.cc` SDK is compiled **four separate times** during the build (once for each layer target).
- **Self-Contained Dynamic Library Constraints**: Because Vulkan layers are deployed as independent dynamic libraries (`.so`/`.dll`), each layer using Perfetto must remain fully self-contained (avoiding transitive `.so` dependencies that break Android/standalone loading). Therefore, deduplication must focus on build-time compilation savings and source-level boilerplate rather than external shared objects.
- **Build Script Duplication**: `layersvt/CMakeLists.txt` contains repetitive 100+ line loops with ad-hoc branching for JSON generation, platform definitions, and install targets.

### Goals of the Refactoring
1. **Zero-Overhead Common Core**: Extract shared layer infrastructure into a lightweight static/object library (`layersvt_common`) linked into each self-contained layer shared object.
2. **Standardized Lifecycle & Dispatch (`LayerBaseClass`)**: Provide a unified, thread-safe base class for loader chain unwrapping, dispatch table lookups, and physical device association for `DebugMarker`, `CPUTiming`, `DeviceMemoryReport`, and `screenshot`.
3. **Shared Perfetto Helper Code**: Unify Perfetto initialization wrappers and session observers at the source level across layers.
4. **Consistent Platform Support**: Ensure Android keep-alive (`RTLD_NODELETE`) and extension workarounds are universally applied.
5. **Simplified Build Configuration**: Replace repetitive CMake code with a clean, declarative CMake function (e.g. `vt_add_layer`).
6. **Preserve Compatibility**: Maintain full backwards compatibility with Vulkan loader expectations, layer manifest JSONs, and test suites.

---

## 2. Inventory of Current Duplication & Inconsistencies

| Component / Concern | `debug_marker` (IN-SCOPE) | `cputiming` (IN-SCOPE) | `device_memory_report` (IN-SCOPE) | `screenshot` (IN-SCOPE) | `monitor` [EXCLUDED] | `api_dump` [EXCLUDED] |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Loader Chain Unwrapping (`get_chain_info`)** | `vk_layer_table.cpp` | `vk_layer_table.cpp` | `vk_layer_table.cpp` | `vk_layer_table.cpp` | `vk_layer_table.cpp` | `vk_layer_table.cpp` |
| **Instance/Device Table Storage** | `vk_layer_table.cpp` | `vk_layer_table.cpp` | `vk_layer_table.cpp` | `vk_layer_table.cpp` + custom `dispatchMap` | `vk_layer_table.cpp` + custom `layer_data_map` | `vk_layer_table.cpp` (global map) |
| **`VkPhysicalDevice` &rarr; `VkInstance` Map** | `DebugMarker::vk_instance_map_` | `CpuTiming::vk_instance_map_` | `DeviceMemoryReport::vk_instance_map_` | `physDeviceMap` | `layer_instances` | None |
| **GIPA / GDPA Interception** | Handwritten `if (strcmp(...))` | Python-generated function | Handwritten `if (strcmp(...))` | Handwritten array loop | Macro `ADD_HOOK` (`strncmp`) | Python-generated switch / tables |
| **Property Enumeration (`vkEnumerate...`)** | Static structs + Android merge fix | Static structs + `util_Get...` | Static structs + `util_Get...` | Static structs + `util_Get...` | Static structs + `util_Get...` | Static structs + `util_Get...` |
| **Tooling Info (`ToolPropertiesEXT`)** | None | None | None | Handwritten | Handwritten | None |
| **Perfetto Tracing Initialization** | `InitializeDebugMarkerPerfetto()` | `InitializePerfetto()` | `InitializeDeviceMemoryReportPerfetto()` | `InitializeScreenshotsPerfetto()` | None | None |
| **Perfetto SDK Compilation** | Compiles `perfetto.cc` | Compiles `perfetto.cc` | Compiles `perfetto.cc` | Compiles `perfetto.cc` | No | No |
| **Android Keep-Alive (`RTLD_NODELETE`)** | Linked (`layer_keep_alive.cpp`) | Linked (`layer_keep_alive.cpp`) | Linked (`layer_keep_alive.cpp`) | Linked (`layer_keep_alive.cpp`) | Missing | Missing |
| **Layer Settings Integration** | None (needed for filtering) | Hardcoded | None | `vkuCreateLayerSettingSet` | `vkuCreateLayerSettingSet` | `ApiDumpInstance` / `LayerSettings` |

---

## 3. Deep Dive into Duplicated Subsystems

### 3.1. Physical Device to Instance Association
* **Why it is needed**: In `vkCreateDevice(VkPhysicalDevice gpu, ...)`, the layer must call the next layer's `vkCreateDevice`. But `pfnNextGetInstanceProcAddr` requires a `VkInstance` handle (`fpGetInstanceProcAddr(instance, "vkCreateDevice")`).
* **Current Implementation**:
  - `layersvt/debug_marker/debug_marker.h` and `debug_marker.cpp`:
    ```cpp
    std::mutex mutex_;
    std::unordered_map<VkPhysicalDevice, VkInstance> vk_instance_map_;
    void SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance);
    VkInstance GetVkInstance(VkPhysicalDevice phys_dev);
    ```
  - `layersvt/cpu_timing/cputiming.h` and `cputiming.cpp`:
    ```cpp
    std::mutex map_mutex_;
    std::unordered_map<VkPhysicalDevice, VkInstance> vk_instance_map_;
    void SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance);
    VkInstance GetVkInstance(VkPhysicalDevice phys_dev);
    ```
  - `layersvt/monitor.cpp`:
    ```cpp
    static std::unordered_map<VkPhysicalDevice, VkInstance> layer_instances;
    // Accessed directly in vkCreateDevice, populated in vkEnumeratePhysicalDevices (unlocked!)
    ```
  - `layersvt/screenshot/screenshot.cpp`:
    ```cpp
    typedef struct { VkInstance instance; } PhysDeviceMapStruct;
    static unordered_map<VkPhysicalDevice, PhysDeviceMapStruct> physDeviceMap;
    ```
* **Solution**: Create a shared, thread-safe `DeviceInstanceTracker` class in `layersvt_common`. Provide an automatic eager enumeration helper in `vkCreateInstance` to populate physical devices immediately upon instance creation.

### 3.2. Vulkan Loader Chain Traversal & Dispatch Table Initialization
* **Current Implementation**:
  Every layer repeats the exact same code pattern in `vkCreateInstance`, `vkDestroyInstance`, `vkCreateDevice`, and `vkDestroyDevice`:
  ```cpp
  // Repeated in all layers:
  VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
  PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
  chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
  VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
  if (result == VK_SUCCESS) {
      initInstanceTable(*pInstance, fpGetInstanceProcAddr);
  }
  ```
* **Thread Safety Bug in `vk_layer_table.cpp`**:
  `vk_layer_table.cpp` stores `tableMap` and `tableInstanceMap` as raw static `std::unordered_map` instances without mutexes. Concurrent calls to `vkCreateDevice` or dispatch lookups can result in undefined behavior and data races.
* **Solution**:
  - Encapsulate dispatch table maps inside a synchronized `DispatchTableManager` with `std::shared_mutex` (reader/writer lock for fast dispatch key lookup).
  - Provide a standardized `LayerLifecycleHelper` that handles chain traversal, next pointer advancement, table initialization, and cleanup.

### 3.3. Layer & Extension Property Enumeration
* **Current Implementation**:
  - `util_GetLayerProperties` and `util_GetExtensionProperties` exist in `vk_layer_table.h`, but each layer still manually writes:
    - `vkEnumerateInstanceLayerProperties`
    - `vkEnumerateDeviceLayerProperties`
    - `vkEnumerateInstanceExtensionProperties`
    - `vkEnumerateDeviceExtensionProperties`
  - In `debug_marker/debug_marker_handwritten_functions.h` (lines 207&ndash;247), a 40-line workaround is required for Android bug b/143293104 where implicit layers do not expose device extensions to the loader. This requires querying downstream device extensions, deduplicating, and appending layer extensions. Other layers lack this fix.
* **Solution**:
  - Provide a declarative `LayerManifest` descriptor:
    ```cpp
    struct LayerMetadata {
        const char* layer_name;
        uint32_t spec_version;
        uint32_t implementation_version;
        const char* description;
        std::vector<VkExtensionProperties> instance_extensions;
        std::vector<VkExtensionProperties> device_extensions;
        std::optional<VkPhysicalDeviceToolPropertiesEXT> tool_properties;
    };
    ```
  - Standard enumeration functions in `layersvt_common` can service all five queries automatically using the metadata.

### 3.4. GIPA / GDPA Interception Dispatch
* **Current Implementation**:
  - `debug_marker`: Linear `strcmp` checks in `debug_marker_known_instance_functions` and `debug_marker_known_device_functions`.
  - `screenshot`: Custom static tables with linear loop scan.
  - `monitor`: Fragile `ADD_HOOK` macro using `!strncmp(#fn, funcName, sizeof(#fn))`.
  - `cputiming` & `api_dump`: Python-generated lookup functions.
* **Solution**:
  - Unify dispatch resolution: a fast lookup table (e.g. compile-time hash map or sorted array with `std::lower_bound` / string-view lookup, or standard generator template).
  - Standardize fallback to downstream dispatch tables:
    ```cpp
    PFN_vkVoidFunction CommonGetInstanceProcAddr(VkInstance instance, const char* pName,
                                                 PFN_vkVoidFunction (*layer_hook_lookup)(const char*));
    ```

### 3.5. Perfetto Tracing Integration
* **Current Implementation**:
  - `cputiming`, `debug_marker`, and `screenshot` all initialize Perfetto using nearly identical blocks:
    ```cpp
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;
    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();
    ```
  - Each layer defines its own static storage and invokes it via `std::call_once(g_perfetto_init_flag, ...)` in `vkCreateInstance`.
  - Both `debug_marker` and `screenshot` use `perfetto::TrackEventSessionObserver` to react to tracing sessions starting and stopping (e.g., re-emitting object names or pausing/resuming screenshot capture).
* **Constraint & Nuance with Dynamic Libraries**:
  - Because each layer is distributed and loaded as an independent dynamic library (`.so`/`.dll`), layers **must remain self-contained**.
  - Creating a separate shared library (`liblayersvt_perfetto.so`) is **not viable** because Vulkan layers are frequently deployed individually (e.g. pushed to `/data/local/debug/vulkan/` or bundled in standalone APKs). Introducing transitive `.so` dependencies causes dynamic linker resolution failures unless complex `RPATH` / `LD_LIBRARY_PATH` gymnastics are enforced.
  - Consequently, **on-disk binary footprint cannot be deduplicated across separate `.so` files**; each layer using Perfetto must embed the compiled Perfetto engine.
* **What Common Abstraction Solves**:
  1. **Source Code Deduplication & Consistency**: Standardize boilerplate (`InitializePerfetto`, backend configuration for `traced`, `std::call_once` lifecycle management, and `TrackEventSessionObserver` callbacks for late-attach or capture control) across layers rather than copy-pasting the implementation.
  2. **Process-Level Coexistence**: When an application activates multiple Perfetto-enabled layers simultaneously (e.g., `CPUTiming` and `DebugMarker`), ensure their independent producer sessions and track event registrations interact predictably with the system `traced` daemon.

### 3.6. Android Keep-Alive (`RTLD_NODELETE`)
* **Current Implementation**:
  - `layer_keep_alive.cpp` uses a static constructor to call `dlopen(info.dli_fname, RTLD_NODELETE)` on Android so the dynamic linker does not unload the layer when `dlclose` is called by applications or test runners.
  - Only linked into `screenshot`, `cputiming`, and `debug_marker`.
* **Solution**:
  - Move keep-alive support into `layersvt_common` so that every layer automatically benefits from `RTLD_NODELETE` on Android.

### 3.7. Cross-Platform Logging & Android Logcat Integration
* **Current Problem**:
  - `cputiming.cpp`: defines custom constructor/destructor calling `__android_log_print(ANDROID_LOG_WARN, "CPUTiming", ...)`.
  - `screenshot_perfetto.cpp`: calls `__android_log_print(ANDROID_LOG_INFO, "screenshot", ...)`.
  - `debug_marker.cpp`: outputs to `stdout` / `stderr`.
* **Solution**:
  - Provide a standardized logging header (`layersvt/common/log.h`) with `LOGI`, `LOGW`, and `LOGE` macros:
    - On Android: routes to `__android_log_print` using the layer's tag.
    - On Desktop (Linux/Windows): routes to `std::cerr` / `std::cout` with clean formatting and timestamps.

### 3.8. Windows Headless / CI Dialog Suppression
* **Current Problem**:
  - `screenshot.cpp` (lines 1487&ndash;1497) disables blocking assertion dialogs (`_set_abort_behavior(0, ...)`, `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`).
  - `debug_marker` and `cputiming` omitted this, meaning assertion failures or crashes on Windows in CI/headless runners can pop up modal message boxes that hang the test runner.
* **Solution**:
  - Standardize in `LayerBase::CreateInstance` so all layers automatically suppress modal crash/assert dialogs on Windows.

### 3.9. Layer Settings & Configuration Management (`Vulkan::LayerSettings`)
* **Current Problem**:
  - `screenshot` integrates with `vulkan/layer/vk_layer_settings.hpp` (`VkuLayerSettingSet`) to parse frame numbers, scale, and output directories.
  - `cputiming` and `debug_marker` currently hardcode their settings or rely on compile-time constants, even though users frequently need to configure verbosity, category masks, or output destinations via `vk_layer_settings.txt` or environment variables.
* **Solution**:
  - Provide a common `LayerSettingsHelper` in `layersvt_common` with typed getters (`GetBool`, `GetInt`, `GetString`, `GetStringList`) wrapping `VkuLayerSettingSet` uniformly.

### 3.10. Vulkan Handle Keying & 32/64-bit Architecture Safety
* **Current Problem**:
  - Each layer casts handles to `uint64_t` or `void*` inconsistently:
    - On 64-bit platforms, both dispatchable and non-dispatchable handles are 64-bit values.
    - On 32-bit platforms (e.g. `armeabi-v7a` or `x86`), dispatchable handles are 32-bit pointers (`void*`), while non-dispatchable handles are 64-bit integers (`uint64_t`).
    - Casting between `void*` and `uint64_t` causes compiler warnings or truncation bugs if not guarded with `reinterpret_cast<uint64_t>` or `uintptr_t`.
* **Solution**:
  - Define a shared `CastToUint64(Handle)` or `HandleKey<T>` helper to guarantee portable, warning-free handle hashing across 32-bit and 64-bit ABIs.

### 3.11. Windows Export `.def` Files & Symbol Visibility
* **Current Problem**:
  - `debug_marker/VkLayer_DebugMarker.def` and `screenshot/VkLayer_screenshot.def` have identical exported function lists (`vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkEnumerateInstanceLayerProperties`, `vkEnumerateInstanceExtensionProperties`).
  - `cputiming` has no `.def` file and relies on CMake symbol visibility.
  - Each layer defines its own `EXPORT_FUNCTION` macro with `__attribute__((visibility("default")))`.
* **Solution**:
  - Provide a single canonical `VkLayer_exports.def.in` template configured by CMake for all layers.
  - Standardize `VK_LAYER_EXPORT` in `layersvt_common.h`.

### 3.12. Decoupled Test Harness Infrastructure
* **Current Problem**:
  - In `layersvt/test/CMakeLists.txt`, `DebugMarker` tests had to compile `../debug_marker/debug_marker.cpp` directly because the layer only existed as a shared `.so` target without a separate linkable core.
* **Solution**:
  - Structure each in-scope layer as a core static/object library (`VkLayer_<name>_core`) linked by both the dynamic `.so` and the unit test executable (`test_<name>_layer`), eliminating redundant compilation in tests.

---

## 4. Layer Feasibility Analysis: Convertibility to Common Initialization

Below is a detailed feasibility breakdown for each layer:

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   LAYER CONVERTIBILITY SPECTRUM                                 │
├──────────────────────────┬─────────────────────────────────────┬────────────────────────────────┤
│ Zero-Override Adoption   │ Hook-Based Adoption                 │ Excluded from Refactoring      │
│ (Pure LayerBase)         │ (LayerBase + OnDeviceCreated Hooks) │ (Preserved As-Is)              │
├──────────────────────────┼─────────────────────────────────────┼────────────────────────────────┤
│ • VkLayer_DebugMarker    │ • VkLayer_DeviceMemoryReport        │ • VkLayer_monitor              │
│ • VkLayer_CPUTiming      │   (reuses common CreateDevice;      │   (desktop XCB/Win32 only)     │
│                          │    injects pNext report callback)   │ • VkLayer_api_dump             │
│                          │ • VkLayer_screenshot                │   (recursive parameter dump,   │
│                          │   (reuses common CreateDevice;      │    templated on ApiDumpFormat) │
│                          │    uses OnDeviceCreated for WSI)    │                                │
└──────────────────────────┴─────────────────────────────────────┴────────────────────────────────┘
```

---

### 4.1. Zero-Override Adoption: `VkLayer_DebugMarker` & `VkLayer_CPUTiming`
* **Status**: **Direct Turnkey Adoption**
* **Why**:
  - Pure instrumentation/intercept layers.
  - They do **not** allocate internal Vulkan dispatchable objects (no command buffers or images created inside the layer).
  - They do **not** interact with the windowing system or surface/swapchain creation.
  - Their initialization steps in `vkCreateInstance` / `vkCreateDevice` are identical:
    1. Unwind loader chain (`get_chain_info`) and advance link info.
    2. Call downstream `fpCreateInstance` / `fpCreateDevice`.
    3. Initialize dispatch table (`initInstanceTable` / `initDeviceTable`).
    4. Register `VkPhysicalDevice` &rarr; `VkInstance` mapping.
    5. Optionally initialize Perfetto once (`std::call_once`).
* **Conversion Plan**:
  Both layers can completely delete their handwritten `vkCreateInstance`, `vkCreateDevice`, and dispatch table management, inheriting directly from `LayerBase` with no hook overrides needed.

---

### 4.2. Hook-Based Adoption: `VkLayer_DeviceMemoryReport`
* **Status**: **Reuses Common `CreateDevice` + Callback Injection Hook**
* **What can be unified**:
  - Unwinding loader chain info in `vkCreateInstance` and `vkCreateDevice`.
  - Dispatch table initialization (`initInstanceTable` / `initDeviceTable`).
  - Replacing custom `DeviceMemoryReport::vk_instance_map_` with `DeviceInstanceTracker`.
  - Perfetto initialization and session observer patterns.
  - Android `RTLD_NODELETE` keep-alive.
* **Hook Requirements**:
  - In `CreateDevice`: Checks if the driver supports `VK_EXT_device_memory_report`, injects `VkDeviceDeviceMemoryReportCreateInfoEXT` into `pCreateInfo->pNext`, and queries `VkPhysicalDeviceMemoryProperties` on the created device via `OnDeviceCreated`.
* **Conclusion**:
  `DeviceMemoryReport` fits seamlessly into `LayerBaseClass` via an `OnDeviceCreated` post-hook and create info injection, eliminating over 200 lines of boilerplate.

---

### 4.3. Hook-Based Adoption: `VkLayer_screenshot`
* **Can we reuse basic functionality for `CreateDevice`, or do we need our full own implementation?**
  **We can reuse almost the ENTIRE `CreateDevice` implementation! We do NOT need a separate implementation.**
* **Analysis of `screenshot`'s `CreateDevice`**:
  - 85% of `screenshot`'s [`CreateDevice`](file:///usr/local/google/home/okuznetsov/prj/VulkanTools/layersvt/screenshot/screenshot.cpp#L1552-L1596) is identical to every other layer:
    1. Resolving `VkInstance` from `gpu` (`physDeviceMap`).
    2. Unwinding `VkLayerDeviceCreateInfo` with `VK_LAYER_LINK_INFO`.
    3. Querying downstream `fpCreateDevice` via `fpGetInstanceProcAddr(instance, "vkCreateDevice")`.
    4. Advancing `chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext`.
    5. Calling downstream `fpCreateDevice(...)`.
    6. Initializing device dispatch table (`vkuInitDeviceDispatchTable`).
    7. Mapping `device` &rarr; `physicalDevice`.
  - What makes `screenshot` unique in `CreateDevice`?
    1. **`VK_LOADER_DATA_CALLBACK`**:
       `screenshot` queries `chain_info = get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK)` to save `pfnSetDeviceLoaderData`. Why? Because `screenshot` creates internal dispatchable objects (images, command buffers) to read back swapchain images, requiring `pfnSetDeviceLoaderData` to initialize their loader dispatch tables.
       **Solution**: `LayerBase::CreateDevice` can extract and save `VK_LOADER_DATA_CALLBACK` **automatically** for any device! It costs one pointer check during chain walking and benefits any layer that ever creates internal objects.
    2. **Swapchain Extension Detection**:
       `screenshot` scans `pCreateInfo->ppEnabledExtensionNames` for `VK_KHR_SWAPCHAIN_EXTENSION_NAME` to set `wsi_enabled`.
       **Solution**: Handled cleanly in `OnDeviceCreated(device, physicalDevice, pCreateInfo)`.
* **Conclusion for `screenshot`**:
  `screenshot` does **not** need its own `CreateDevice`. It uses `LayerBase::CreateDevice` directly and only provides a small `OnDeviceCreated` hook.

---

### 4.4. Excluded from Refactoring: `VkLayer_monitor` & `VkLayer_api_dump`

#### A. `VkLayer_monitor` &mdash; Excluded
* **Rationale**:
  - **Desktop-Only Legacy**: It is strictly limited to Windows (Win32) and Linux (XCB) to append FPS text to window title bars.
  - **No Mobile/Android Relevance**: Android has no window title bars, Win32 `HWND`s, or XCB connections, making this layer completely non-functional on Android.
  - **Zero Maintenance / Feature Velocity**: The code is mature and dormant. Refactoring it would risk regressions in legacy desktop XCB code without providing value to modern profiling workflows.

#### B. `VkLayer_api_dump` &mdash; Excluded
* **Rationale**:
  - **Specialized Code-Generation Architecture**: `api_dump` is driven by a massive generator (`scripts/generators/api_dump_generator.py`) and generates ~75,000 lines of templated code (`generated/api_dump_dispatch.h` and `generated/api_dump_implementation.h`).
  - **Runtime Format Specialization**: Every function is templated (`template <ApiDumpFormat Format>`) and GIPA/GDPA dynamically selects function pointer tables based on runtime format (`Text`, `Html`, `Json`).
  - **Self-Dumping Requirement**: It must dump `vkCreateInstance` and `vkCreateDevice` arguments and return values.
  - **Conclusion**: Attempting to force `api_dump` into `LayerBaseClass` would either require a heavy, intrusive visitor framework or break its existing templated code generator. Leaving `api_dump` as-is keeps `layersvt_common` lean, lightweight, and focused.

---

## 5. Proposed Architecture: `layersvt_common`

### 4.1. Directory Structure

```
layersvt/
├── common/                                 # NEW: Common layer support library
│   ├── CMakeLists.txt                      # Target: layersvt_common (STATIC or OBJECT)
│   ├── layer_base.h / .cpp                 # LayerBase abstraction (lifecycle, GIPA/GDPA)
│   ├── dispatch_table_manager.h / .cpp     # Thread-safe dispatch table maps & keys
│   ├── device_instance_tracker.h / .cpp    # Thread-safe VkPhysicalDevice -> VkInstance mapping
│   ├── layer_manifest.h / .cpp             # Property enumeration & Android b/143293104 fix
│   ├── layer_keep_alive.h / .cpp           # Android RTLD_NODELETE keep-alive
│   ├── perfetto_manager.h / .cpp           # Unified Perfetto init & session management
│   └── layer_settings_util.h / .cpp        # LayerSettings config helpers
├── cmake/
│   └── LayersVTMacros.cmake                # Standardized macro: vt_add_layer(...)
├── api_dump/                               # Existing layers (refactored to use common)
├── cpu_timing/
├── debug_marker/
├── monitor/
├── screenshot/
├── perfetto/                               # Shared Perfetto SDK (perfetto.cc / perfetto.h)
└── test/                                   # Layer unit tests
```

### 4.2. Core Classes and Interfaces

#### 1. `DispatchTableManager`
```cpp
namespace layersvt {

class DispatchTableManager {
public:
    static DispatchTableManager& Get();

    // Key calculation (matches Vulkan loader conventions)
    static void* GetDispatchKey(const void* object) {
        return *(void**)object;
    }

    // Instance dispatch tables
    VkuInstanceDispatchTable* InitInstanceTable(VkInstance instance, PFN_vkGetInstanceProcAddr gpa);
    VkuInstanceDispatchTable* GetInstanceTable(const void* object);
    void DestroyInstanceTable(const void* object);

    // Device dispatch tables
    VkuDeviceDispatchTable* InitDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr gpa);
    VkuDeviceDispatchTable* GetDeviceTable(const void* object);
    void DestroyDeviceTable(const void* object);

private:
    std::shared_mutex instance_mutex_;
    std::unordered_map<void*, std::unique_ptr<VkuInstanceDispatchTable>> instance_tables_;

    std::shared_mutex device_mutex_;
    std::unordered_map<void*, std::unique_ptr<VkuDeviceDispatchTable>> device_tables_;
};

} // namespace layersvt
```

#### 2. `DeviceInstanceTracker`
```cpp
namespace layersvt {

class DeviceInstanceTracker {
public:
    static DeviceInstanceTracker& Get();

    void SetInstance(VkPhysicalDevice physical_device, VkInstance instance);
    VkInstance GetInstance(VkPhysicalDevice physical_device);
    void RemoveInstance(VkInstance instance);

    // Eagerly queries and maps all physical devices for the instance
    void EagerMapDevices(VkInstance instance, PFN_vkGetInstanceProcAddr gpa);

private:
    std::shared_mutex mutex_;
    std::unordered_map<VkPhysicalDevice, VkInstance> device_to_instance_;
};

} // namespace layersvt
```

#### 3. `LayerManifest` & Properties Handler
```cpp
namespace layersvt {

struct LayerMetadata {
    const char* layer_name;
    uint32_t spec_version;
    uint32_t implementation_version;
    const char* description;
    std::vector<VkExtensionProperties> instance_extensions;
    std::vector<VkExtensionProperties> device_extensions;
    std::optional<VkPhysicalDeviceToolPropertiesEXT> tool_properties;
};

class LayerManifest {
public:
    explicit LayerManifest(LayerMetadata metadata);

    VkResult EnumerateInstanceLayerProperties(uint32_t* count, VkLayerProperties* props);
    VkResult EnumerateDeviceLayerProperties(uint32_t* count, VkLayerProperties* props);
    VkResult EnumerateInstanceExtensionProperties(const char* layer_name, uint32_t* count, VkExtensionProperties* props);
    VkResult EnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device, const char* layer_name,
                                                uint32_t* count, VkExtensionProperties* props);
    VkResult GetPhysicalDeviceToolProperties(VkPhysicalDevice physical_device, uint32_t* count,
                                             VkPhysicalDeviceToolPropertiesEXT* props);

private:
    LayerMetadata metadata_;
};

} // namespace layersvt
```

#### 4. `LayerBase` Template / Helper
```cpp
namespace layersvt {

template <typename Derived>
class LayerBase {
public:
    static VkResult CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance);

    static void DestroyInstance(VkInstance instance,
                                const VkAllocationCallbacks* pAllocator);

    static VkResult CreateDevice(VkPhysicalDevice physicalDevice,
                                 const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkDevice* pDevice);

    static void DestroyDevice(VkDevice device,
                              const VkAllocationCallbacks* pAllocator);

    static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* pName);
    static PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* pName);
};

} // namespace layersvt
```

---

## 6. CMake Build Modernization

### 6.1. Creating `layersvt_common` Static Target
In `layersvt/common/CMakeLists.txt`:
```cmake
add_library(layersvt_common STATIC
    dispatch_table_manager.cpp
    device_instance_tracker.cpp
    layer_manifest.cpp
    layer_keep_alive.cpp
    perfetto_manager.cpp
)

target_include_directories(layersvt_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/layersvt
)

target_link_libraries(layersvt_common PUBLIC
    Vulkan::Headers
    Vulkan::UtilityHeaders
    Vulkan::LayerSettings
)

if (ANDROID)
    target_link_libraries(layersvt_common PUBLIC log android)
endif()
```

### 6.2. Standard Layer Creation Macro: `vt_add_layer`
In `layersvt/cmake/LayersVTMacros.cmake`:
```cmake
function(vt_add_layer LAYER_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        "ENABLE_PERFETTO"
        "FOLDER;DEF_FILE;JSON_IN"
        "SOURCES;INCLUDE_DIRS;DEFINITIONS;LIBRARIES"
    )

    add_library(${LAYER_NAME} MODULE ${ARG_SOURCES})
    set_target_properties(${LAYER_NAME} PROPERTIES FOLDER ${ARG_FOLDER})

    target_include_directories(${LAYER_NAME} PRIVATE ${ARG_INCLUDE_DIRS})
    target_compile_definitions(${LAYER_NAME} PRIVATE ${ARG_DEFINITIONS})
    target_link_libraries(${LAYER_NAME} PRIVATE layersvt_common ${ARG_LIBRARIES})

    if (ARG_ENABLE_PERFETTO)
        target_link_libraries(${LAYER_NAME} PRIVATE layersvt_perfetto)
    endif()

    # Linker scripts / def files
    if (MSVC AND ARG_DEF_FILE)
        target_link_options(${LAYER_NAME} PRIVATE /DEF:${ARG_DEF_FILE})
    elseif(MINGW AND ARG_DEF_FILE)
        target_sources(${LAYER_NAME} PRIVATE ${ARG_DEF_FILE})
    endif()

    # Standard JSON manifest configuration & installation
    vt_configure_layer_json(${LAYER_NAME} ${ARG_JSON_IN})
endfunction()
```

This single macro replaces over 120 lines of brittle `foreach` and `if(STREQUAL ...)` logic in `layersvt/CMakeLists.txt`.

---

## 7. Sequential PR Implementation Roadmap (Option A)

Each PR is self-contained, builds cleanly across Linux and Android, passes all unit tests, and can be merged into `upstream/main` before the subsequent PR is submitted.

### PR 1: Core Foundation (`layersvt_common`) & Unit Tests
* **Goal**: Establish the common layer support library without touching existing layers yet, ensuring zero risk of regression.
* **Deliverables**:
  - [x] Create `layersvt/common/` directory.
  - [x] `device_instance_tracker.h` / `.cpp`: Thread-safe `VkPhysicalDevice` &rarr; `VkInstance` tracker (`std::shared_mutex`) with eager device enumeration.
  - [x] `dispatch_table_manager.h` / `.cpp`: Thread-safe instance and device dispatch table storage.
  - [x] `layer_manifest.h` / `.cpp`: Layer metadata and extension property queries (incorporating Android b/143293104 fix).
  - [x] `layer_base.h` / `.cpp`: Standardized `LayerBase` class providing uniform `vkCreateInstance` / `vkCreateDevice` / `GIPA` / `GDPA` with runtime validation, teardown ordering, and extensible virtual hooks.
  - [x] `layer_keep_alive.h` / `.cpp`: Generalized Android `RTLD_NODELETE` keep-alive with explicit `EnsureLayerKeepAlive()`.
  - [x] `log.h`: Cross-platform `LOGI`, `LOGW`, `LOGE` macros (Android logcat + desktop stderr).
  - [x] `layersvt/common/CMakeLists.txt`: Defines `layersvt_common` CMake `OBJECT` library.
  - [x] Add unit tests in `layersvt/test/test_common.cpp` verifying thread safety, instance tracking, eager mapping, dispatch table lookups, layer manifest queries, and layer lifecycle under mock loader.

### PR 2: Refactor Google Profiling Layers (`DebugMarker`, `CPUTiming`, `DeviceMemoryReport`)
* **Goal**: Migrate the 3 Google profiling layers to use `layersvt_common`, eliminating duplicate maps and handwritten lifecycle functions.
* **Deliverables**:
  - [ ] **`DebugMarker`**: Delete custom `vk_instance_map_` and handwritten `vkCreateInstance`/`vkCreateDevice`; inherit from `LayerBase`; link `layersvt_common`.
  - [ ] **`CPUTiming`**: Delete custom `vk_instance_map_`; inherit from `LayerBase` (using timer hooks in `PreCreateDevice`/`PostCreateDevice`); link `layersvt_common`.
  - [ ] **`DeviceMemoryReport`**: Delete custom `vk_instance_map_`; inherit from `LayerBase` (injecting `VK_EXT_device_memory_report` callback in `OnDeviceCreated`); link `layersvt_common`.
  - [ ] Verify all tests pass (`test_debugmarker`, `test_cputiming`, `test_devicememoryreport`).

### PR 3: Refactor Screenshot Layer (`VkLayer_screenshot`)
* **Goal**: Refactor `screenshot.cpp` to inherit from `LayerBase`.
* **Deliverables**:
  - [ ] Replace custom `physDeviceMap` with `DeviceInstanceTracker`.
  - [ ] Replace custom `dispatchMap` with `DispatchTableManager`.
  - [ ] Inherit from `LayerBase::CreateDevice`, using `OnDeviceCreated` to check `VK_KHR_SWAPCHAIN_EXTENSION_NAME` for WSI state.
  - [ ] Use `DispatchTableManager::GetDeviceLoaderDataCallback(*pDevice)` for initializing internal dispatchable handles (`pfnSetDeviceLoaderData`).
  - [ ] Replace manual `intercept_core_...` loops with unified `LayerBase` dispatch.
  - [ ] Verify test passes (`test_screenshot`).

### PR 4: CMake Modernization (`vt_add_layer` Macro)
* **Goal**: Replace 120+ lines of repetitive CMake loops with a clean declarative macro.
* **Deliverables**:
  - [ ] Implement `vt_add_layer` macro in `layersvt/cmake/LayersVTMacros.cmake`.
  - [ ] Refactor target definitions for `VkLayer_DebugMarker`, `VkLayer_CPUTiming`, `VkLayer_DeviceMemoryReport`, and `VkLayer_screenshot` to use `vt_add_layer`.
  - [ ] Keep `VkLayer_monitor` and `VkLayer_api_dump` build targets intact as-is.
  - [ ] Deprecate internal per-layer `vk_layer_table.cpp` compilation for in-scope layers.

---

## 8. Verification & Testing Strategy

To ensure zero regressions across platforms:
1. **Linux Build & Test**: Run `./scripts/github_ci_linux.py` to build all layers and execute `ctest`.
2. **Android Verification**:
   - Build Android APK targets using the Android skill procedures (`build-android/`).
   - Validate layer loading and ensure `RTLD_NODELETE` prevents premature unloading.
3. **Multi-Thread Concurrency Test**:
   - Add a stress test in `layersvt/test/` that creates/destroys multiple `VkDevice`s and command buffers across multiple threads simultaneously to prove dispatch table thread-safety.
4. **Perfetto Tracing Validation**:
   - Validate that `DebugMarker`, `CPUTiming`, and `Screenshot` produce valid Perfetto packets when multiple layers are enabled concurrently.
