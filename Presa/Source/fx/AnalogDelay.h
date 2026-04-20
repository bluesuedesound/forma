#pragma once
#include <juce_dsp/juce_dsp.h>

// Bucket-brigade-style delay. The LPF in the feedback path is the
// whole point: each repeat gets a little darker, like a BBD chip.
class AnalogDelay
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    void setTimeMs    (float ms)  noexcept;            // 1..800
    void setFeedback  (float fb)  noexcept;            // 0..0.85 (hard-limited)
    void setToneHz    (float hz)  noexcept;            // feedback LPF cutoff, 3k..8k typical
    void setMix       (float m)   noexcept;            // 0..1

    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    float readSample (int ch, float delaySamples) noexcept;

    std::vector<float> bufL, bufR;
    int   bufSize = 0;
    int   writePos = 0;

    float targetDelaySamples = 0.0f;
    float currentDelaySamples = 0.0f;    // smoothed to avoid zipper noise

    float feedback = 0.0f;
    float mix      = 0.0f;

    juce::dsp::IIR::Filter<float> fbLpL, fbLpR;
    float toneHz = 5000.0f;
    double sr = 44100.0;
};
