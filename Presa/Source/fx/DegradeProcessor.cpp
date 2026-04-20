#include "DegradeProcessor.h"

void DegradeProcessor::prepare (double sampleRate, int blockSize, int /*numChannels*/)
{
    sr = sampleRate;

    // 8 ms of headroom. Centre the delay line at 4 ms so the LFO
    // can push earlier or later without ever reading the write pointer.
    delaySize = juce::nextPowerOfTwo ((int) (0.008 * sampleRate) + 4);
    delayL.assign ((size_t) delaySize, 0.0f);
    delayR.assign ((size_t) delaySize, 0.0f);
    writePos = 0;
    centerDelaySamples = (float) (0.004 * sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };

    wow.initialise     ([] (float x) { return std::sin (x); }, 256);
    flutter.initialise ([] (float x) { return std::sin (x); }, 256);
    wow    .prepare (spec); wow    .setFrequency (0.5f);   // 0.3-0.7 Hz; 0.5 is a good centre
    flutter.prepare (spec); flutter.setFrequency (10.0f);  // 8-12 Hz

    lpL.prepare (spec); lpR.prepare (spec);
    currentCutoff = 20000.0f;
    auto lp = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (sampleRate, currentCutoff);
    *lpL.coefficients = *lp;
    *lpR.coefficients = *lp;

    saturator.prepare (sampleRate, blockSize, 2);
    saturator.setTone (0.4f);
    saturator.setMix  (1.0f);

    reset();
}

void DegradeProcessor::reset()
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
    wow.reset(); flutter.reset();
    lpL.reset(); lpR.reset();
    saturator.reset();
}

float DegradeProcessor::stageWowFlutter (float a) noexcept
{
    return a <= 0.0f ? 0.0f : juce::jmin (1.0f, a / 0.25f);
}

float DegradeProcessor::stageLowpass (float a) noexcept
{
    if (a <= 0.25f) return 0.0f;
    return juce::jmin (1.0f, (a - 0.25f) / (0.60f - 0.25f));
}

float DegradeProcessor::stageSaturation (float a) noexcept
{
    if (a <= 0.60f) return 0.0f;
    return juce::jmin (1.0f, (a - 0.60f) / (0.85f - 0.60f));
}

float DegradeProcessor::stageNoise (float a) noexcept
{
    if (a <= 0.85f) return 0.0f;
    return juce::jmin (1.0f, (a - 0.85f) / (1.00f - 0.85f));
}

void DegradeProcessor::writeDelay (int ch, float x) noexcept
{
    auto& buf = (ch == 0 ? delayL : delayR);
    buf[(size_t) writePos] = x;
}

float DegradeProcessor::readDelay (int ch, float delaySamples) noexcept
{
    auto& buf = (ch == 0 ? delayL : delayR);
    float readPos = (float) writePos - delaySamples;
    while (readPos < 0.0f)          readPos += (float) delaySize;
    while (readPos >= (float) delaySize) readPos -= (float) delaySize;

    const int i0 = (int) readPos;
    const int i1 = (i0 + 1) % delaySize;
    const float frac = readPos - (float) i0;
    return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
}

void DegradeProcessor::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    if (amount <= 0.0f) return;

    const float wowStage  = stageWowFlutter (amount);
    const float lpStage   = stageLowpass    (amount);
    const float satStage  = stageSaturation (amount);
    const float noiseStg  = stageNoise      (amount);

    // Wow depth doubles once we enter the noise stage (dying machine).
    const float wowDepthPct     = 0.003f * wowStage * (1.0f + noiseStg);   // ±0.3 % .. ±0.6 %
    const float flutterDepthPct = 0.0005f * wowStage;                      // ±0.05 %

    // Convert percentage speed variation into delay-time modulation depth (samples).
    // At a centre delay of 4 ms, ±0.3% corresponds to ~±0.012 samples at 48 kHz —
    // that's far too small to hear. We scale into a meaningful ±2.5-sample swing
    // at full wow so the timebase wobble is audible but not pitchy.
    const float wowDepthSamples     = wowDepthPct     * centerDelaySamples * 200.0f;
    const float flutterDepthSamples = flutterDepthPct * centerDelaySamples * 200.0f;

    // Update LP cutoff for this block (cheap — one coefficient recompute).
    const float targetCutoff = 18000.0f + (6000.0f - 18000.0f) * lpStage;
    if (std::abs (targetCutoff - currentCutoff) > 50.0f)
    {
        currentCutoff = targetCutoff;
        auto lp = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (sr, currentCutoff);
        *lpL.coefficients = *lp;
        *lpR.coefficients = *lp;
    }

    saturator.setDrive (satStage);
    saturator.setMix   (satStage);

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();

    // Noise floor: -60 dB at noiseStg=0 onset, ramping to -40 dB at full.
    const float noiseDb   = -60.0f + 20.0f * noiseStg;
    const float noiseGain = noiseStg > 0.0f ? juce::Decibels::decibelsToGain (noiseDb) : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float wowOsc     = wow    .processSample (0.0f);
        const float flutterOsc = flutter.processSample (0.0f);
        const float delaySamples = centerDelaySamples
                                 + wowOsc     * wowDepthSamples
                                 + flutterOsc * flutterDepthSamples;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float x = buffer.getSample (ch, i);
            writeDelay (ch, x);
            float y = readDelay (ch, delaySamples);

            // HF roll-off.
            auto& lp = (ch == 0 ? lpL : lpR);
            y = lp.processSample (y);

            // Saturation (applies internal wet/dry via satStage-driven mix).
            y = saturator.processSample (ch, y);

            // Bandlimited-ish noise (white scaled; the LP above takes some edge off).
            if (noiseGain > 0.0f)
                y += (noiseRng.nextFloat() * 2.0f - 1.0f) * noiseGain;

            buffer.setSample (ch, i, y);
        }

        writePos = (writePos + 1) % delaySize;
    }
}
