#include "screenshots_perfetto_helpers.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && !PERFETTO_BUILDFLAG(PERFETTO_COMPILER_GCC)
namespace perfetto {
// Workaround for a linker error on Windows where the static member definition
// for TrackEventDataSource::type() is missing due to an empty macro in the amalgamated header.
template <>
perfetto::internal::DataSourceType& DataSourceHelper<perfetto::internal::TrackEventDataSource, perfetto::internal::TrackEventDataSourceTraits>::type() {
    static perfetto::internal::DataSourceType type_;
    return type_;
}
} // namespace perfetto
#endif // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && !PERFETTO_BUILDFLAG(PERFETTO_COMPILER_GCC)

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
