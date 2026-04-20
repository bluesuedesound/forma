#include "LoopbackCapture.h"

#if JUCE_MAC

#include "ScreenCaptureAudio.h"

// Thin C++ wrapper around the ObjC++ ScreenCaptureAudio implementation.
// All heavy lifting lives in ScreenCaptureAudio.mm — this file only exists
// to satisfy the LoopbackCapture factory contract used by PresaProcessor.

class LoopbackCapture_Mac : public LoopbackCapture
{
public:
    explicit LoopbackCapture_Mac (CaptureBuffer& buf)
        : capture (buf) {}

    ~LoopbackCapture_Mac() override
    {
        capture.stop();
    }

    juce::String startCapture() override { return capture.start(); }
    void         stopCapture()  override { capture.stop(); }
    bool         isCapturing() const override { return capture.isRunning(); }

    juce::String getDeviceStatus() const override { return capture.getStatus(); }
    double getCaptureSampleRate() const override { return capture.getSampleRate(); }

private:
    ScreenCaptureAudio capture;
};

std::unique_ptr<LoopbackCapture> LoopbackCapture::create (CaptureBuffer& buffer)
{
    return std::make_unique<LoopbackCapture_Mac> (buffer);
}

#endif // JUCE_MAC
