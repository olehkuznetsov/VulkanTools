#ifndef LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H
#define LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H

#include "perfetto.h"

#ifdef ANDROID
#include <android/log.h>
#endif

// Define categories used for Perfetto tracing in the screenshot layer.
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("VulkanScreenshots").SetDescription("Vulkan Layer Screenshots")
);

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

void InitializeScreenshotsPerfetto();

#endif // LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H
