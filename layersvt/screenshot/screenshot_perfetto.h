#ifndef LAYERSVT_SCREENSHOT_PERFETTO_H
#define LAYERSVT_SCREENSHOT_PERFETTO_H

#include "perfetto/perfetto.h"

class ScreenshotDataSource : public perfetto::DataSource<ScreenshotDataSource> {
   public:
    void OnStart(const StartArgs&) override;
    void OnStop(const StopArgs& args) override;
};

void InitializeScreenshotsPerfetto();

#endif  // LAYERSVT_SCREENSHOT_PERFETTO_H
