#include "LoopbackCapture.h"

#if JUCE_WINDOWS

// TODO: WASAPI loopback capture via IMMDeviceEnumerator + IAudioClient
// with AUDCLNT_STREAMFLAGS_LOOPBACK on the default output device.
// For now, this is a stub that compiles but reports "Not implemented".

class LoopbackCapture_Win : public LoopbackCapture
{
public:
    explicit LoopbackCapture_Win (CaptureBuffer&) {}

    void startCapture()  override {}
    void stopCapture()  override {}
    bool isCapturing() const override { return false; }

    juce::String getDeviceStatus() const override
    {
        return "WASAPI loopback: not yet implemented";
    }
};

std::unique_ptr<LoopbackCapture> LoopbackCapture::create (CaptureBuffer& buffer)
{
    return std::make_unique<LoopbackCapture_Win> (buffer);
}

#endif // JUCE_WINDOWS
