#include "FXChain.h"

void FXChain::prepare (double sampleRate, int blockSize, int numChannels)
{
    sat    .prepare (sampleRate, blockSize, numChannels);
    degrade.prepare (sampleRate, blockSize, numChannels);
    filter .prepare (sampleRate, blockSize, numChannels);
    delay  .prepare (sampleRate, blockSize, numChannels);
    reverb .prepare (sampleRate, blockSize, numChannels);

    sat.setTone (0.4f);
    sat.setMix  (1.0f);

    // Everything starts transparent.
    setWornAxis (0.0f);
    setBreathingAxis (0.0f);
    delay.setMix  (0.0f);
    reverb.setMix (0.0f);
    filter.setKnob (1.0f);   // fully open
}

void FXChain::reset()
{
    sat    .reset();
    degrade.reset();
    filter .reset();
    delay  .reset();
    reverb .reset();
}

void FXChain::setWornAxis (float x) noexcept
{
    wornX = juce::jlimit (0.0f, 1.0f, x);

    // Primary: DEGRADE amount follows the axis directly.
    degrade.setAmount (wornX);

    // Secondary: TapeSaturator drive ramps up in the top half of the axis
    // so the first half of the WORN travel is all tape wobble, and the
    // top half adds the nonlinearity on top.
    const float drive = juce::jmax (0.0f, (wornX - 0.3f) / 0.7f);
    sat.setDrive (juce::jlimit (0.0f, 1.0f, drive * 0.7f));

    // Tertiary: Filter closes from fully open (1.0) toward 0.55
    // (roughly 3 kHz) as WORN approaches 1.0. Subtle on its own;
    // stacks with the DEGRADE LP for more convincing oxide wear.
    const float knob = 1.0f - wornX * 0.45f;
    filter.setKnob (knob);
}

void FXChain::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    sat    .processBlock (buffer);
    degrade.processBlock (buffer);
    filter .processBlock (buffer);
    delay  .processBlock (buffer);
    reverb .processBlock (buffer);
}
