#include "FilterModule.h"

void FilterModule::prepare (double sampleRate, int blockSize, int numChannels)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, (juce::uint32) juce::jmax (1, numChannels) };
    ladder.prepare (spec);
    ladder.setMode (juce::dsp::LadderFilterMode::LPF24);
    ladder.setDrive (1.2f);     // slight push into the nonlinearity
    ladder.setCutoffFrequencyHz (20000.0f);
    ladder.setResonance (0.0f);
    reset();
}

void FilterModule::reset()
{
    ladder.reset();
}

void FilterModule::setCutoffHz (float hz) noexcept
{
    // LadderFilter clamps internally but be defensive — going past Nyquist
    // is harmless here but the ladder's tuning gets weird above ~20 kHz.
    hz = juce::jlimit (20.0f, 20000.0f, hz);
    ladder.setCutoffFrequencyHz (hz);
}

void FilterModule::setResonance (float r) noexcept
{
    ladder.setResonance (juce::jlimit (0.0f, 0.9f, r));
}

void FilterModule::setKnob (float k) noexcept
{
    k = juce::jlimit (0.0f, 1.0f, k);
    // Piecewise exponential: 0 → 20 Hz, 0.5 → 2 kHz, 1 → 20 kHz.
    //   f(k) = 20 * 10^(3 * k)  gives 20..20000 with 2k at k≈0.667.
    // To land 2 kHz at the centre (k=0.5) we use two exponential segments.
    const float hz = (k <= 0.5f)
        ? 20.0f  * std::pow (2000.0f / 20.0f,    k / 0.5f)          // 20 → 2000
        : 2000.0f * std::pow (20000.0f / 2000.0f, (k - 0.5f) / 0.5f); // 2000 → 20000
    setCutoffHz (hz);
}

void FilterModule::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    ladder.process (ctx);
}
