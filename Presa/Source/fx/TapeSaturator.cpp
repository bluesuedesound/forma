#include "TapeSaturator.h"

void TapeSaturator::prepare (double sampleRate, int blockSize, int /*numChannels*/)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    toneShelfL.prepare (spec);
    toneShelfR.prepare (spec);
    dcBlockL  .prepare (spec);
    dcBlockR  .prepare (spec);

    // 1-pole HPF at 20 Hz removes the DC the asymmetric shaper introduces.
    auto dcCoeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, 20.0f);
    *dcBlockL.coefficients = *dcCoeffs;
    *dcBlockR.coefficients = *dcCoeffs;

    updateTone();
    reset();
}

void TapeSaturator::reset()
{
    toneShelfL.reset(); toneShelfR.reset();
    dcBlockL  .reset(); dcBlockR  .reset();
}

void TapeSaturator::updateTone()
{
    // tone=0 → flat (0 dB). tone=1 → ~-9 dB shelf above 3 kHz.
    const float gainDb = -9.0f * tone;
    const float gainLin = juce::Decibels::decibelsToGain (gainDb);
    auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 3000.0f, 0.707f, gainLin);
    *toneShelfL.coefficients = *shelf;
    *toneShelfR.coefficients = *shelf;
}

float TapeSaturator::saturate (float x, float gain) noexcept
{
    const float driven = x * gain;
    // Asymmetric soft clip — positive half saturates harder than negative.
    if (driven > 0.0f)
        return 1.0f - std::exp (-driven * 1.2f);
    else
        return -1.0f + std::exp ( driven * 0.8f);
}

float TapeSaturator::processSample (int channel, float x) noexcept
{
    auto& shelf = (channel == 0 ? toneShelfL : toneShelfR);
    auto& dc    = (channel == 0 ? dcBlockL   : dcBlockR);

    const float pre     = shelf.processSample (x);
    const float gain    = 1.0f + drive * 5.0f;
    const float shaped  = saturate (pre, gain);
    const float blocked = dc.processSample (shaped);

    // Compensate for the gain the shaper adds so output level stays sane
    // regardless of drive. The soft-clip asymptotes to ±1, so normalising
    // by the gain keeps mix comparisons honest at low drive too.
    const float wet = blocked / std::sqrt (gain);
    return x * (1.0f - mix) + wet * mix;
}

void TapeSaturator::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            d[i] = processSample (ch, d[i]);
    }
}
