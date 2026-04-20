#include "AnalogDelay.h"

void AnalogDelay::prepare (double sampleRate, int blockSize, int /*numChannels*/)
{
    sr = sampleRate;
    // 900 ms headroom > 800 ms max spec.
    bufSize = juce::nextPowerOfTwo ((int) (0.9 * sampleRate) + 4);
    bufL.assign ((size_t) bufSize, 0.0f);
    bufR.assign ((size_t) bufSize, 0.0f);
    writePos = 0;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    fbLpL.prepare (spec);
    fbLpR.prepare (spec);
    setToneHz (toneHz);

    // Default to ~250 ms so the delay has a sensible time if setMix() is
    // called before setTimeMs().
    currentDelaySamples = targetDelaySamples = (float) (0.25 * sampleRate);

    reset();
}

void AnalogDelay::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
    fbLpL.reset();
    fbLpR.reset();
    currentDelaySamples = targetDelaySamples;
}

void AnalogDelay::setTimeMs (float ms) noexcept
{
    ms = juce::jlimit (1.0f, 800.0f, ms);
    targetDelaySamples = (float) (ms * 0.001 * sr);
}

void AnalogDelay::setFeedback (float fb) noexcept
{
    feedback = juce::jlimit (0.0f, 0.85f, fb);
}

void AnalogDelay::setToneHz (float hz) noexcept
{
    toneHz = juce::jlimit (500.0f, 12000.0f, hz);
    auto lp = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (sr, toneHz);
    *fbLpL.coefficients = *lp;
    *fbLpR.coefficients = *lp;
}

void AnalogDelay::setMix (float m) noexcept
{
    mix = juce::jlimit (0.0f, 1.0f, m);
}

float AnalogDelay::readSample (int ch, float delaySamples) noexcept
{
    auto& buf = (ch == 0 ? bufL : bufR);
    float readPos = (float) writePos - delaySamples;
    while (readPos < 0.0f)              readPos += (float) bufSize;
    while (readPos >= (float) bufSize)  readPos -= (float) bufSize;

    const int i0 = (int) readPos;
    const int i1 = (i0 + 1) % bufSize;
    const float frac = readPos - (float) i0;
    return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
}

void AnalogDelay::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    if (mix <= 0.0f) return;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();

    // Smooth delay-time changes so repitch swoops don't zipper.
    const float smoothing = 0.001f;

    for (int i = 0; i < n; ++i)
    {
        currentDelaySamples += (targetDelaySamples - currentDelaySamples) * smoothing;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float in  = buffer.getSample (ch, i);
            float delayed   = readSample (ch, currentDelaySamples);

            // LPF on the feedback path — this is the analog colour.
            auto& fbLP = (ch == 0 ? fbLpL : fbLpR);
            const float fbFiltered = fbLP.processSample (delayed);

            auto& buf = (ch == 0 ? bufL : bufR);
            buf[(size_t) writePos] = in + fbFiltered * feedback;

            buffer.setSample (ch, i, in * (1.0f - mix) + delayed * mix);
        }

        writePos = (writePos + 1) % bufSize;
    }
}
