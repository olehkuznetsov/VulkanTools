#include "screenshots_perfetto_helpers.h"
#include "screenshot_writer.h"

namespace screenshot {
std::unique_ptr<ScreenshotWriter> globalScreenshotWriter = std::make_unique<FileScreenshotWriter>();
}

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && !PERFETTO_BUILDFLAG(PERFETTO_COMPILER_GCC)
namespace perfetto {
// Workaround for a linker error on Windows where the static member definition
// for TrackEventDataSource::type() is missing due to an empty macro in the amalgamated header.
template <>
perfetto::internal::DataSourceType&
DataSourceHelper<perfetto::internal::TrackEventDataSource, perfetto::internal::TrackEventDataSourceTraits>::type() {
    static perfetto::internal::DataSourceType type_;
    return type_;
}
}  // namespace perfetto
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && !PERFETTO_BUILDFLAG(PERFETTO_COMPILER_GCC)

void InitializeScreenshotsPerfetto() {
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
    screenshot::globalScreenshotWriter = std::make_unique<screenshot::PerfettoScreenshotWriter>();
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
