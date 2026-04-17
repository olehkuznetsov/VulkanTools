#ifndef LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H
#define LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H

#include "perfetto.h"
#include <mutex>
#include <condition_variable>
#include <list>
#include <memory>
#include <atomic>
#include <thread>

#ifdef ANDROID
#include <android/log.h>
#endif

// Define categories used for Perfetto tracing in the screenshot layer.
PERFETTO_DEFINE_CATEGORIES(perfetto::Category("VulkanScreenshots").SetDescription("Vulkan Layer Screenshots"));

namespace screenshot {
struct ScreenshotQueueData;
extern std::atomic_bool pauseCapture;
extern std::mutex globalLock;
extern std::condition_variable screenshotSavedCV;
extern std::list<std::shared_ptr<ScreenshotQueueData>> screenshotsData;
}  // namespace screenshot

class ScreenshotDataSource : public perfetto::DataSource<ScreenshotDataSource> {
   public:
    void OnStart(const StartArgs&) override;
    void OnStop(const StopArgs& args) override;
};

void InitializeScreenshotsPerfetto();

#endif  // LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H
