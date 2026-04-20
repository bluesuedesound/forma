#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "../CaptureBuffer.h"

class LoopbackCapture
{
public:
    virtual ~LoopbackCapture() = default;

    // Returns empty string on success, error message on failure
    virtual juce::String startCapture() = 0;
    virtual void stopCapture()  = 0;
    virtual bool isCapturing() const = 0;

    // Human-readable device status for the top bar
    virtual juce::String getDeviceStatus() const = 0;

    // Sample rate at which the capture backend is delivering audio.
    // Defaults to the configured rate (48 kHz for SCK); updated on the first
    // audio callback to reflect what the OS actually delivered.
    virtual double getCaptureSampleRate() const = 0;

    static std::unique_ptr<LoopbackCapture> create (CaptureBuffer& buffer);
};
