#include "AmbientEngine.h"

namespace
{
    // One-pole LPF coefficient: α = 1 - e^(-2π·fc/Fs).
    float onePoleAlpha (float cutoffHz, double Fs) noexcept
    {
        const float wc = juce::MathConstants<float>::twoPi
                       * juce::jmax (20.0f, cutoffHz)
                       / (float) Fs;
        return 1.0f - std::exp (-wc);
    }
}

void AmbientEngine::prepare (double sr, int /*blockSize*/, int /*numChannels*/)
{
    sampleRate = sr;

    // 4-second circular buffer gives plenty of headroom for speed-drift
    // depth (up to ±8 % of baseDelaySamples) without ever colliding with
    // the write pointer.
    bufSize = juce::nextPowerOfTwo ((int) (4.0 * sr));
    bufL.assign ((size_t) bufSize, 0.0f);
    bufR.assign ((size_t) bufSize, 0.0f);
    writePos = 0;

    baseDelaySamples = 0.5f * (float) sr;   // 500 ms centre

    reset();
}

void AmbientEngine::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;

    pitchPhase = speedPhase = timbrePhase = 0.0f;
    lpStateL = lpStateR = 0.0f;
    currentCutoff = 18000.0f;
    feedbackGain = 0.0f;
}

float AmbientEngine::readDelayed (int channel, float delaySamples) const noexcept
{
    const auto& buf = (channel == 0 ? bufL : bufR);
    float readPos = (float) writePos - delaySamples;
    while (readPos < 0.0f)               readPos += (float) bufSize;
    while (readPos >= (float) bufSize)   readPos -= (float) bufSize;

    const int   i0   = (int) readPos;
    const int   i1   = (i0 + 1) % bufSize;
    const float frac = readPos - (float) i0;
    return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
}

void AmbientEngine::processBlock (juce::AudioBuffer<float>& buffer,
                                  float dotX, float dotY)
{
    dotX = juce::jlimit (0.0f, 1.0f, dotX);
    dotY = juce::jlimit (0.0f, 1.0f, dotY);

    const bool focused = (dotX < 0.2f && dotY < 0.2f);

    // ── Feedback gain: silent in FOCUSED quadrant, else scales with dot.
    //   Clamped to 0.82 so long tails sustain but never runaway.
    //   Final safety clamp 0.85 below — the nominal target can't exceed it.
    float targetGain = focused ? 0.0f
                               : juce::jlimit (0.0f, 0.82f,
                                               dotX * 0.4f + dotY * 0.4f);

    // Smooth feedback gain changes to avoid zipper noise at block boundaries.
    const float gainSmoothing = 0.02f;
    feedbackGain += (targetGain - feedbackGain) * gainSmoothing;
    feedbackGain  = juce::jlimit (0.0f, 0.85f, feedbackGain);

    // ── LFO rates / depths scale with dotX (DRIFT).
    const float pitchLfoHz = 0.05f + dotX * 0.15f;
    const float speedLfoHz = 0.02f + dotX * 0.06f;
    const float timbreLfoHz = 0.03f;   // gentle wobble on cutoff

    // 15 cents ≈ factor 2^(15/1200) − 1 ≈ 0.00867. Expressed as a
    // delay-modulation depth at 500 ms centre this is hundreds of
    // samples — plenty for audible wander without tearing.
    const float pitchCents   = dotX * 15.0f;
    const float pitchRatio   = std::pow (2.0f, pitchCents / 1200.0f) - 1.0f;
    const float pitchDepthS  = pitchRatio * baseDelaySamples;

    // Speed drift: ±8 % of centre delay at full DRIFT.
    const float speedDepthS  = dotX * 0.08f * baseDelaySamples;

    // ── Timbral drift: cutoff falls at (dotY * 200 Hz) per second,
    // resets to 18 kHz when the dot returns to FOCUSED.
    const int n = buffer.getNumSamples();

    if (focused)
    {
        currentCutoff = 18000.0f;
    }
    else
    {
        const float dropHzPerSec = dotY * 200.0f;
        const float blockSec     = (float) n / (float) sampleRate;
        currentCutoff = juce::jmax (800.0f, currentCutoff - dropHzPerSec * blockSec);
    }

    // Entirely silent — skip the DSP but still drain the buffer so stale
    // state doesn't snap back when the engine reengages.
    if (targetGain <= 0.0f && feedbackGain < 0.0001f)
    {
        // Age the buffer by one block of zeros so the next engagement
        // starts from silence, not a 500-ms-stale signal.
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = 0.0f;
            bufR[(size_t) writePos] = 0.0f;
            writePos = (writePos + 1) % bufSize;
        }
        lpStateL = lpStateR = 0.0f;
        return;
    }

    const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float) sampleRate;
    const float pitchStep  = pitchLfoHz  * twoPiOverSr;
    const float speedStep  = speedLfoHz  * twoPiOverSr;
    const float timbreStep = timbreLfoHz * twoPiOverSr;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int i = 0; i < n; ++i)
    {
        pitchPhase  += pitchStep;   if (pitchPhase  > juce::MathConstants<float>::twoPi) pitchPhase  -= juce::MathConstants<float>::twoPi;
        speedPhase  += speedStep;   if (speedPhase  > juce::MathConstants<float>::twoPi) speedPhase  -= juce::MathConstants<float>::twoPi;
        timbrePhase += timbreStep;  if (timbrePhase > juce::MathConstants<float>::twoPi) timbrePhase -= juce::MathConstants<float>::twoPi;

        const float pitchMod = std::sin (pitchPhase);
        const float speedMod = std::sin (speedPhase);
        const float timbreMod = std::sin (timbrePhase);

        // Effective delay read position.
        const float delay = baseDelaySamples
                          + pitchMod * pitchDepthS
                          + speedMod * speedDepthS;

        // Per-sample cutoff: drift target ±80 Hz wobble so the darkening
        // breathes rather than sliding dead-linearly.
        const float effCutoff = juce::jmax (600.0f,
                                            currentCutoff + timbreMod * 80.0f);
        const float alpha = onePoleAlpha (effCutoff, sampleRate);

        // Read feedback tap with linear interpolation.
        float fL = readDelayed (0, delay);
        float fR = (numCh > 1) ? readDelayed (1, delay) : fL;

        // One-pole LPF (in-place on retained state).
        lpStateL += alpha * (fL - lpStateL);
        lpStateR += alpha * (fR - lpStateR);
        fL = lpStateL;
        fR = lpStateR;

        fL *= feedbackGain;
        fR *= feedbackGain;

        // Soft-clip the feedback tap so any transient excursion can't
        // scream even if numerics push the sum out of range.
        fL = std::tanh (fL);
        fR = std::tanh (fR);

        // Input samples.
        const float inL = buffer.getSample (0, i);
        const float inR = (numCh > 1) ? buffer.getSample (1, i) : inL;

        // Write (input + feedback) into buffer so the tap loops.
        bufL[(size_t) writePos] = inL + fL;
        bufR[(size_t) writePos] = inR + fR;

        // Output = input + feedback (wet + dry).
        buffer.setSample (0, i, inL + fL);
        if (numCh > 1)
            buffer.setSample (1, i, inR + fR);

        writePos = (writePos + 1) % bufSize;
    }
}
