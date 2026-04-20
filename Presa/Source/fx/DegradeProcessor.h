#pragma once
#include <juce_dsp/juce_dsp.h>
#include "TapeSaturator.h"

// Single-knob analog degradation sweep.
//
//   0%            transparent
//   0-25%         wow/flutter only (variable delay line)
//   25-60%        + HF roll-off sweeping 18 kHz → 6 kHz
//   60-85%        + tape saturation (even harmonics dominate)
//   85-100%       + noise floor (-60 → -40 dBFS) and wow depth doubles
//
// Implemented as a circular buffer with linear-interpolated read and
// LFO-driven delay-time modulation — more faithful than pitch shifting.
class DegradeProcessor
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    // Single normalised control 0..1.
    void setAmount (float a) noexcept { amount = juce::jlimit (0.0f, 1.0f, a); }

    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    // Variable delay read with linear interpolation (per channel).
    float readDelay (int ch, float delaySamples) noexcept;
    void  writeDelay (int ch, float x) noexcept;

    // Helpers mapping amount 0..1 to each stage's activation (0..1).
    static float stageWowFlutter (float a) noexcept;   // 0 below 0, 1 above 0
    static float stageLowpass    (float a) noexcept;   // 0 below 0.25, 1 at 0.60
    static float stageSaturation (float a) noexcept;   // 0 below 0.60, 1 at 0.85
    static float stageNoise      (float a) noexcept;   // 0 below 0.85, 1 at 1.00

    float amount = 0.0f;
    double sr    = 44100.0;

    // Delay line — 8 ms is plenty for ±0.3 % wow at the sample rate ceiling.
    std::vector<float> delayL, delayR;
    int   delaySize = 0;
    int   writePos  = 0;
    float centerDelaySamples = 0.0f;

    // Wow (slow) and flutter (fast) LFOs.
    juce::dsp::Oscillator<float> wow, flutter;

    // HF roll-off — one-pole LPF per channel.
    juce::dsp::IIR::Filter<float> lpL, lpR;
    float currentCutoff = 20000.0f;

    // Saturation stage.
    TapeSaturator saturator;

    // Noise generator.
    juce::Random noiseRng;
};
