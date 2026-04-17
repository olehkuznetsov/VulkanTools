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

    virtual void updateLayerSettings(VkuLayerSettingSet layerSettingSet) {}
    virtual bool isPerfetto() const { return false; }
    virtual void setInProgress() {}
    virtual void setInPause() {}
    virtual bool isPaused() const { return false; }
    virtual bool canControlPause() const { return false; }
};

class FileScreenshotWriter : public ScreenshotWriter {
   public:
    bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) override;

    void updateLayerSettings(VkuLayerSettingSet layerSettingSet) override;
    void setInProgress() override;
    void setInPause() override;
    bool isPaused() const override;
    bool canControlPause() const override { return true; }

   private:
    bool pauseFileRecorded = false;
};

class PerfettoScreenshotWriter : public ScreenshotWriter {
   public:
    PerfettoScreenshotWriter();
    bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) override;
    bool isPerfetto() const override { return true; }
    void setInProgress() override;
    void setInPause() override;
    bool isPaused() const override;
   private:
    bool pauseFileRecorded = false;
};

extern std::unique_ptr<ScreenshotWriter> screenshotWriter;

}  // namespace screenshot

#endif  // LAYERSVT_SCREENSHOT_WRITER_H
