#ifndef LAYERSVT_SCREENSHOT_WRITER_H
#define LAYERSVT_SCREENSHOT_WRITER_H

#include <memory>

namespace screenshot {

class ScreenshotWriter {
   public:
    virtual ~ScreenshotWriter() = default;
    virtual bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) = 0;

    virtual void controlPause() {}
    virtual void setInProgress() {}
    virtual void setInPause() {}
    virtual bool isPaused() const { return false; }
    virtual bool canControlPause() const { return false; }
};

class FileScreenshotWriter : public ScreenshotWriter {
   public:
    bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) override;

    void controlPause() override;
    void setInProgress() override;
    void setInPause() override;
    bool isPaused() const override;
    bool canControlPause() const override;

   private:
    bool pauseFileRecorded = false;
};

class PerfettoScreenshotWriter : public ScreenshotWriter {
   public:
    PerfettoScreenshotWriter();
    bool write(const char* pixels, int width, int height, int numChannels, int rowPitch, int frameNumber) override;
    void setInProgress() override;
    void setInPause() override;
    bool isPaused() const override;

   private:
    bool pauseFileRecorded = false;
};

}  // namespace screenshot

#endif  // LAYERSVT_SCREENSHOT_WRITER_H
