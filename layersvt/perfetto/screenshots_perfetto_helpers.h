#ifndef LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H
#define LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H

#include "perfetto.h"

class ScreenshotDataSource : public perfetto::DataSource<ScreenshotDataSource> {
   public:
    void OnStart(const StartArgs&) override;
    void OnStop(const StopArgs& args) override;
};

void InitializeScreenshotsPerfetto();

#endif  // LAYERSVT_SCREENSHOTS_PERFETTO_HELPERS_H
