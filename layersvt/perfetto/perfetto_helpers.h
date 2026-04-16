#ifndef LAYERSVT_PERFETTO_HELPERS_H
#define LAYERSVT_PERFETTO_HELPERS_H

#include "perfetto.h"

// Define categories used for Perfetto tracing.
// These static strings are registered with Perfetto at initialization time.
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("VulkanCPUTiming/VkInstance").SetDescription("Instance Level Commands"),
    perfetto::Category("VulkanCPUTiming/VkPhysicalDevice").SetDescription("Physical Device Level Commands"),
    perfetto::Category("VulkanCPUTiming/VkDevice").SetDescription("Device Level Commands"),
    perfetto::Category("VulkanCPUTiming/VkQueue").SetDescription("Queue Level Commands"),
    perfetto::Category("VulkanCPUTiming/VkCommandBuffer").SetDescription("Command Buffer Level Commands"),
    perfetto::Category("VulkanCPUTiming/Other").SetDescription("Other Commands"),
    perfetto::Category("application_screenshot").SetDescription("Vulkan Layer Screenshots")
);

// Enum matching the Perfetto categories above.
// This allows us to use static switch statements in the Timer class instead of
// dynamic string lookups, which would be significantly slower.
enum class CPUTimingCategory {
    VkInstance,
    VkPhysicalDevice,
    VkDevice,
    VkQueue,
    VkCommandBuffer,
    Other
};

class ScreenshotDataSource : public perfetto::DataSource<ScreenshotDataSource> {
 public:
  void OnSetup(const SetupArgs&) override {}
  void OnStart(const StartArgs&) override {
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "screenshot", "ScreenshotDataSource::OnStart called");
#endif
  }
  void OnStop(const StopArgs&) override {
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "screenshot", "ScreenshotDataSource::OnStop called");
#endif
  }
};

void InitializePerfetto();

#endif // LAYERSVT_PERFETTO_HELPERS_H
