#ifndef LAYERSVT_SCREENSHOT_WRITER_H
#define LAYERSVT_SCREENSHOT_WRITER_H

#include <vulkan/vulkan.h>
#include <vulkan/layer/vk_layer_settings.hpp>
#include <memory>

namespace screenshot {
class ScreenshotQueueData;
}

namespace screenshot {

class ScreenshotWriter {
   public:
    virtual ~ScreenshotWriter() = default;
    virtual bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) = 0;
    virtual void updatePauseState(bool paused, bool queueEmpty) {}
    virtual void updateLayerSettings(VkuLayerSettingSet layerSettingSet) {}
};

class FileScreenshotWriter : public ScreenshotWriter {
   public:
    bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) override;
    void updatePauseState(bool paused, bool queueEmpty) override;
    void updateLayerSettings(VkuLayerSettingSet layerSettingSet) override;

   private:
    bool pauseFileRecorded = false;
};

class PerfettoScreenshotWriter : public ScreenshotWriter {
   public:
    bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) override;
};

extern std::unique_ptr<ScreenshotWriter> globalScreenshotWriter;

}  // namespace screenshot

#endif  // LAYERSVT_SCREENSHOT_WRITER_H
