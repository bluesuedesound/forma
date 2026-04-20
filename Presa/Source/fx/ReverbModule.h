#pragma once
#include <juce_dsp/juce_dsp.h>

// Plate-character reverb: juce::dsp::Reverb tuned for a bright, short
// plate, preceded by a 15 ms fixed pre-delay and a 120 Hz highpass
// (plates are thin in the low end).
class ReverbModule
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    void setMix (float m) noexcept;   // 0..1 wet/dry

    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters params;

    juce::dsp::IIR::Filter<float> hpL, hpR;

    // Fixed 15 ms pre-delay — simple circular buffers, stereo.
    std::vector<float> preL, preR;
    int preSize  = 0;
    int preDelay = 0;
    int preWrite = 0;

    float mix = 0.0f;
    double sr = 44100.0;
};
