#pragma once
#include <juce_dsp/juce_dsp.h>

// Asymmetric soft-clip waveshaper modelling tape oxide saturation.
// Pre-saturation high-shelf cut tames harshness before the nonlinearity
// (like tape head frequency response); DC blocker at ~20 Hz removes the
// asymmetric offset the shaper introduces.
class TapeSaturator
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    // drive 0..1  → internal gain 1..6
    // tone  0..1  → pre-sat high-shelf cut 0 .. ~-9 dB above 3 kHz
    // mix   0..1  → wet/dry
    void setDrive (float d) noexcept { drive = juce::jlimit (0.0f, 1.0f, d); }
    void setTone  (float t) noexcept { tone  = juce::jlimit (0.0f, 1.0f, t); updateTone(); }
    void setMix   (float m) noexcept { mix   = juce::jlimit (0.0f, 1.0f, m); }

    // In-place per-sample processing for a single channel.
    float processSample (int channel, float x) noexcept;

    // In-place block processing (stereo-safe; uses per-channel filter state).
    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    static float saturate (float x, float gain) noexcept;
    void updateTone();

    float drive = 0.0f;
    float tone  = 0.0f;
    float mix   = 1.0f;
    double sr   = 44100.0;

    // Pre-sat high-shelf (tone) and post-sat DC-block HPF per channel.
    juce::dsp::IIR::Filter<float> toneShelfL, toneShelfR;
    juce::dsp::IIR::Filter<float> dcBlockL,   dcBlockR;
};
