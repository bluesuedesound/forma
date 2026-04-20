#pragma once

#include <atomic>
#include <juce_core/juce_core.h>
#include "../CaptureBuffer.h"

// ScreenCaptureKit-based system audio capture (macOS 13+).
// Objective-C++ implementation lives in ScreenCaptureAudio.mm.
// Public surface is plain C++ so the rest of the codebase stays untouched.

class ScreenCaptureAudio
{
public:
    explicit ScreenCaptureAudio (CaptureBuffer& buffer);
    ~ScreenCaptureAudio();

    // Returns empty string on success, error message on failure.
    juce::String start();
    void stop();
    bool isRunning() const { return running.load(); }

    juce::String getStatus() const;

    // Sample rate that ScreenCaptureKit is delivering, updated from the ASBD
    // of the first audio callback. 48 kHz until proven otherwise.
    double getSampleRate() const { return deliveredSampleRate.load(); }

    // Called from the ObjC++ side when the stream reports a terminal error.
    void reportError (const juce::String& message);
    void setStatus   (const juce::String& message);
    void markRunning (bool r);
    void setDeliveredSampleRate (double sr) { deliveredSampleRate.store (sr); }

    CaptureBuffer& getCaptureBuffer() { return captureBuffer; }

private:
    CaptureBuffer& captureBuffer;

    // Opaque ObjC handle — defined in the .mm file.
    void* impl = nullptr;

    std::atomic<bool>   running { false };
    std::atomic<double> deliveredSampleRate { 48000.0 };

    mutable juce::CriticalSection statusLock;
    juce::String status { "Idle" };
};
