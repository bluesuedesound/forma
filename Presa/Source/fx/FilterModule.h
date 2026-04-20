#pragma once
#include <juce_dsp/juce_dsp.h>

// 24 dB/oct Moog-ladder lowpass (juce::dsp::LadderFilter).
// Defaults fully open so it's transparent until used.
class FilterModule
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    // Direct values.
    void setCutoffHz   (float hz) noexcept;
    void setResonance  (float r)  noexcept;    // 0..0.9

    // Knob mapping: 0..1 where 0.5 = 2 kHz, 1.0 = 20 kHz (open).
    void setKnob       (float k)  noexcept;

    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::LadderFilter<float> ladder;
    double sr = 44100.0;
};
