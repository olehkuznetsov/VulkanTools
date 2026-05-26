#include "screenshot_perfetto.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>

#ifdef ANDROID
#include <android/log.h>
#endif

#include "screenshot_writer.h"

namespace screenshot {
struct ScreenshotQueueData;
extern std::atomic_bool pauseCapture;
extern std::mutex globalLock;
extern std::condition_variable screenshotSavedCV;
extern std::list<std::shared_ptr<ScreenshotQueueData>> screenshotsData;
}  // namespace screenshot

PERFETTO_DEFINE_CATEGORIES(perfetto::Category("VulkanScreenshots").SetDescription("Vulkan Layer Screenshots"));

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

void InitializeScreenshotsPerfetto() {
    std::atomic_store(&screenshot::pauseCapture, true);

    perfetto::TracingInitArgs args;
    // The backends determine where trace events are recorded.
    // kSystemBackend connects to the system traced service (e.g. on Android).
    args.backends = perfetto::kSystemBackend;

    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();

    perfetto::DataSourceDescriptor dsd;
    dsd.set_name("VulkanScreenshots");
    ScreenshotDataSource::Register(dsd);
}

void ScreenshotDataSource::OnStart(const StartArgs&) {
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "screenshot", "ScreenshotDataSource::OnStart called");
#endif
    std::atomic_store(&screenshot::pauseCapture, false);
}

void ScreenshotDataSource::OnStop(const StopArgs& args) {
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "screenshot", "ScreenshotDataSource::OnStop called");
#endif

    std::atomic_store(&screenshot::pauseCapture, true);
    auto async_stop_closure = args.HandleStopAsynchronously();

    std::thread([stop_closure = std::move(async_stop_closure)]() {
        {
            std::unique_lock<std::mutex> lock(screenshot::globalLock);
            screenshot::screenshotSavedCV.wait(lock, [] { return screenshot::screenshotsData.empty(); });
        }

        // Explicitly notify Perfetto that this data source is now ready to be destroyed.
        stop_closure();
    }).detach();
}
