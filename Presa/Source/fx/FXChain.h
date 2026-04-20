#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "TapeSaturator.h"
#include "DegradeProcessor.h"
#include "FilterModule.h"
#include "AnalogDelay.h"
#include "ReverbModule.h"

// Global analog-modelled FX chain driven by the selected pad's
// StateDot (dotX = WORN, dotY = BREATHING).
//
// Order:  TapeSaturator → Degrade → Filter → AnalogDelay → ReverbModule
//
// All modules default to transparent. The chain becomes audible only
// when the StateDot moves toward WORN or params are explicitly dialled in.
class FXChain
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    // StateDot X axis — LIGHT (0) → WORN (1).
    // Drives DEGRADE (primary), TapeSaturator drive (secondary),
    // Filter cutoff reduction from fully open (tertiary).
    void setWornAxis (float x) noexcept;

    // StateDot Y axis — FIXED (0) → BREATHING (1).
    // Consumed by the sampler (start-point scatter, stutter rate,
    // LFO depth to start point) — stored here so the editor has one
    // place to push dot state. The FX chain itself doesn't need it.
    void setBreathingAxis (float y) noexcept { breathingY = juce::jlimit (0.0f, 1.0f, y); }

    float getBreathingAxis() const noexcept { return breathingY; }

    // Direct overrides (for UI/automation on top of the dot wiring).
    // Each is additive with, not overriding, the WORN-axis mapping —
    // call these only when the user explicitly dials in the module.
    void setDelayMix  (float m) noexcept { delay.setMix  (m); }
    void setDelayTimeMs   (float ms) noexcept { delay.setTimeMs   (ms); }
    void setDelayFeedback (float fb) noexcept { delay.setFeedback (fb); }
    void setReverbMix (float m) noexcept { reverb.setMix (m); }

    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    TapeSaturator    sat;
    DegradeProcessor degrade;
    FilterModule     filter;
    AnalogDelay      delay;
    ReverbModule     reverb;

    float wornX      = 0.0f;
    float breathingY = 0.0f;
};
