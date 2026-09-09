---
name: test-android-layer
description: >-
  Builds, deploys, and verifies Vulkan layers (DebugMarker, DeviceMemoryReport, FpsOverlay,
  LimitExtensions, Screenshot, etc.) on a connected Android device. Deploys layers via app native
  libraries with SELinux context (bypassing Android 14/15 restrictions), enables global GPU debug
  layers, launches the target workload (e.g. Boss Room Unity sample or Sherlock), captures a
  Perfetto trace (GPU render stages + VulkanDebugMarker), and validates debug names and crash-free
  execution via TraceProcessor SQL. Use when asked to test, validate, or profile a Vulkan layer on
  Android, verify debug names/markers, or capture Perfetto traces with Vulkan layers.
metadata:
  icon: 📱
---

# Vulkan Layer Android Testing & Verification Skill

Use this skill to build, deploy, and verify any Vulkan layer in `VulkanTools` (`VK_LAYER_GOOGLE_DebugMarker`, `VK_LAYER_GOOGLE_DeviceMemoryReport`, `VK_LAYER_GOOGLE_FpsOverlay`, `VK_LAYER_GOOGLE_LimitExtensions`, `VK_LAYER_GOOGLE_Screenshot`) on a connected Android device against real workloads (e.g. Boss Room Unity sample or Sherlock layer app).

---

## 1. Prerequisites & Environment Setup

1. **Connected Device**:
   Verify an ADB device is connected and responsive:
   ```bash
   adb devices
   ```

2. **Android SDK & NDK**:
   Ensure `ANDROID_HOME` and `ANDROID_NDK_HOME` (NDK 29+) are exported, and CMake (3.22.1+) / Ninja are on `PATH`:
   ```bash
   export ANDROID_HOME=/usr/local/google/home/okuznetsov/Android/Sdk
   export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/29.0.14206865
   export PATH=$ANDROID_HOME/cmake/3.22.1/bin:$PATH
   ```

---

## 2. Build the Vulkan Layer for Android

From the repository root (`/usr/local/google/home/okuznetsov/prj/VulkanTools`):

```bash
python3 scripts/android.py --config Release --app-abi arm64-v8a
```

The compiled shared libraries will be placed in:
`build-android/install/arm64-v8a/lib/libVkLayer_<LayerName>.so`

Available layers:
- `libVkLayer_DebugMarker.so` (`VK_LAYER_GOOGLE_DebugMarker`)
- `libVkLayer_DeviceMemoryReport.so` (`VK_LAYER_GOOGLE_DeviceMemoryReport`)
- `libVkLayer_FpsOverlay.so` (`VK_LAYER_GOOGLE_FpsOverlay`)
- `libVkLayer_LimitExtensions.so` (`VK_LAYER_GOOGLE_LimitExtensions`)
- `libVkLayer_Screenshot.so` (`VK_LAYER_GOOGLE_Screenshot`)

---

## 3. Deploy Layer to Android (Bypassing Android 14/15 SELinux Restrictions)

> [!IMPORTANT]
> On Android 14 and 15, pushing layers to `/data/local/debug/vulkan/` often fails or is blocked by SELinux for release or third-party applications. The reliable deployment technique is pushing directly into the target application's native library directory and restoring the `apk_data_file` SELinux label.

### Step 3.1: Locate the Target Application's Native Library Directory

```bash
PACKAGE_NAME="com.Unity.com.unity.multiplayer.samples.coop" # or target app package
APP_DIR=$(adb shell pm path "$PACKAGE_NAME" | head -n 1 | sed 's/package://;s/\/base.apk//')
LIB_DIR="$APP_DIR/lib/arm64"
```

Common target packages:
- **Boss Room Unity Sample**: `com.Unity.com.unity.multiplayer.samples.coop`
- **Sherlock Layers App**: `com.google.androidperformanceanalyzer`

### Step 3.2: Push and Fix SELinux Context

```bash
LAYER_SO="libVkLayer_DebugMarker.so"
LOCAL_SO="build-android/install/arm64-v8a/lib/$LAYER_SO"

# Push directly to target app's lib directory
adb push "$LOCAL_SO" "$LIB_DIR/$LAYER_SO"

# Fix permissions and SELinux label so the app sandbox can load it
adb shell chcon u:object_r:apk_data_file:s0 "$LIB_DIR/$LAYER_SO"
adb shell chmod 755 "$LIB_DIR/$LAYER_SO"
```

---

## 4. Enable the Layer in Android Graphics Environment

Set the global layer properties and Android `Settings` hooks:

```bash
LAYER_NAME="VK_LAYER_GOOGLE_DebugMarker"

adb shell setprop debug.vulkan.layers "$LAYER_NAME"
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app "$PACKAGE_NAME"
adb shell settings put global gpu_debug_layers "$LAYER_NAME"
```

---

## 5. Launch the Workload and Verify Layer Loading

Clear logcat, stop any existing instance, and launch the application:

```bash
adb shell am force-stop "$PACKAGE_NAME"
adb shell logcat -c

# Launch main activity
adb shell monkey -p "$PACKAGE_NAME" -c android.intent.category.LAUNCHER 1
sleep 3
```

Verify that the Vulkan loader successfully attached the layer:

```bash
adb logcat -d | grep -iE "vulkan.*Loaded layer|vulkan.*added global layer"
```

Expected logcat snippet:
```log
vulkan  : searching for layers in '/data/app/.../lib/arm64'
vulkan  : added global layer 'VK_LAYER_GOOGLE_DebugMarker' from library '/data/app/.../lib/arm64/libVkLayer_DebugMarker.so'
vulkan  : Loaded layer VK_LAYER_GOOGLE_DebugMarker
```

---

## 6. Capture Perfetto Trace (Render Stages + Vulkan Debug Markers)

Create a 3-second Perfetto capture configuration targeting GPU render stages and Vulkan debug marker events:

```bash
cat << 'EOF' > /tmp/perfetto_layer_config.txt
buffers: {
    size_kb: 65536
    fill_policy: RING_BUFFER
}
data_sources: {
    config {
        name: "gpu.renderstages"
    }
}
data_sources: {
    config {
        name: "track_event"
        track_event_config {
            enabled_categories: "VulkanDebugMarker"
        }
    }
}
duration_ms: 3000
EOF

adb push /tmp/perfetto_layer_config.txt /data/misc/perfetto-configs/perfetto.txt
adb shell "perfetto --out /data/misc/perfetto-traces/trace.perfetto --txt -c /data/misc/perfetto-configs/perfetto.txt"
adb pull /data/misc/perfetto-traces/trace.perfetto /tmp/trace_layer.perfetto
```

---

## 7. Automated TraceProcessor Verification

Validate the captured trace programmatically using `perfetto.trace_processor` to verify that debug names and render stages are present and the workload did not crash:

```python
#!/usr/bin/env python3
import sys
from perfetto.trace_processor import TraceProcessor, TraceProcessorConfig

trace_file = "/tmp/trace_layer.perfetto"
tp = TraceProcessor(trace=trace_file, config=TraceProcessorConfig(bin_path=None))

# 1. Verify GPU Render Stages
render_stages = list(tp.query("SELECT count(*) as count, name FROM slice WHERE track_id IN (SELECT id FROM gpu_track) GROUP BY name;"))
print("=== GPU Render Stages ===")
for row in render_stages:
    print(f"  {row.name}: {row.count} slices")

# 2. Verify Vulkan API / Debug Marker Events
debug_events = list(tp.query("SELECT count(*) as count FROM slice WHERE name LIKE '%Vk%' OR name LIKE '%Vulkan%' OR category = 'VulkanDebugMarker';"))
print(f"\n=== Vulkan Debug Events ===\n  Total matching slices: {debug_events[0].count if debug_events else 0}")

# 3. Query Object Names if present in args
object_names = list(tp.query("SELECT display_value as name, count(*) as count FROM args WHERE key = 'debug_name' OR key LIKE '%object_name%' GROUP BY display_value LIMIT 15;"))
if object_names:
    print("\n=== Sample Captured Object Names ===")
    for row in object_names:
        print(f"  {row.name} ({row.count} occurrences)")

tp.close()
```

---

## 8. Teardown & Device Cleanup

Reset device layer settings after the test:

```bash
adb shell setprop debug.vulkan.layers ""
adb shell settings delete global enable_gpu_debug_layers
adb shell settings delete global gpu_debug_app
adb shell settings delete global gpu_debug_layers
```
