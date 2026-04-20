#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// 30-second stereo ring buffer for captured system audio.
// Thread-safe: writer (audio callback) and reader (UI / commit) use atomic writeHead.

class CaptureBuffer
{
public:
    CaptureBuffer();

    void prepare (double sampleRate, int numChannels);
    void pushSamples (const float* const* channelData, int numChannels, int numSamples);
    void clear();

    // Read a range from the buffer into dest (0 = most recent sample going backwards)
    void readSamples (juce::AudioBuffer<float>& dest, int startSample, int numSamples) const;

    // ── State machine ─────────────────────────────────────────────────────
    enum class State { Idle, Armed, Capturing };

    void arm();         // Idle → Armed: starts writing (ring always writes), marks ready
    void punch();       // Armed → Capturing: locks punchInSample = current writeHead
    void stop();        // Capturing → Idle: locks punchOutSample

    State getState() const { return state.load(); }

    // After stop(), call commitSlice() to extract the recorded region
    // Returns an owning AudioBuffer with the captured audio.
    juce::AudioBuffer<float> commitSlice();

    // Did we just finish a capture? (edge-triggered flag, cleared after read)
    bool hasNewSlice();

    int    getWriteHead()          const { return writeHead.load(); }
    int    getNumSamplesAvailable() const { return samplesWritten.load(); }
    int    getBufferLength()        const { return bufferLength; }
    double getSampleRate()         const { return sampleRate; }

    // Real-time RMS — captureRMS is updated by pushSamples (LoopbackCapture path),
    // inputRMS is updated by updateInputRMS (processBlock path, unconditional).
    float  getCaptureRMS()  const { return captureRMS.load(); }
    float  getInputRMS()    const { return inputRMS.load(); }

    // Call unconditionally from processBlock to measure input bus signal
    void updateInputRMS (const float* data, int numSamples);

private:
    juce::AudioBuffer<float> buffer;
    std::atomic<int> writeHead { 0 };
    int bufferLength    = 0;
    std::atomic<int> samplesWritten { 0 };
    double sampleRate   = 44100.0;

    // State machine
    std::atomic<State> state { State::Idle };
    int punchInSample   = 0;
    int punchOutSample  = 0;
    std::atomic<bool> sliceReady { false };

    // RMS of most recent pushSamples call (LoopbackCapture)
    std::atomic<float> captureRMS { 0.0f };
    // RMS of most recent processBlock input bus (unconditional)
    std::atomic<float> inputRMS { 0.0f };
    bool hasLoggedFirstAudio = false;

    static constexpr double kMaxSeconds = 30.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureBuffer)
};
