#include "ReverbModule.h"

void ReverbModule::prepare (double sampleRate, int blockSize, int numChannels)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, (juce::uint32) juce::jmax (1, numChannels) };
    reverb.prepare (spec);

    params.roomSize   = 0.6f;
    params.damping    = 0.7f;
    params.wetLevel   = 0.0f;
    params.dryLevel   = 1.0f;
    params.width      = 0.8f;
    params.freezeMode = 0.0f;
    reverb.setParameters (params);

    juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) blockSize, 1 };
    hpL.prepare (monoSpec);
    hpR.prepare (monoSpec);
    auto hp = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, 120.0f);
    *hpL.coefficients = *hp;
    *hpR.coefficients = *hp;

    preDelay = (int) std::round (0.015 * sampleRate);
    preSize  = juce::nextPowerOfTwo (preDelay + 4);
    preL.assign ((size_t) preSize, 0.0f);
    preR.assign ((size_t) preSize, 0.0f);
    preWrite = 0;

    reset();
}

void ReverbModule::reset()
{
    reverb.reset();
    hpL.reset(); hpR.reset();
    std::fill (preL.begin(), preL.end(), 0.0f);
    std::fill (preR.begin(), preR.end(), 0.0f);
    preWrite = 0;
}

void ReverbModule::setMix (float m) noexcept
{
    mix = juce::jlimit (0.0f, 1.0f, m);
    params.wetLevel = mix;
    params.dryLevel = 1.0f - mix;
    reverb.setParameters (params);
}

void ReverbModule::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    if (mix <= 0.0f) return;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();

    // Capture dry for a manual wet/dry mix — we feed the reverb a
    // pre-delayed, highpassed signal but want to mix against the
    // untreated dry at the original tap.
    juce::AudioBuffer<float> dry (numCh, n);
    for (int ch = 0; ch < numCh; ++ch)
        dry.copyFrom (ch, 0, buffer, ch, 0, n);

    // Pre-delay + HPF stage, in place.
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& buf   = (ch == 0 ? preL : preR);
            float in    = buffer.getSample (ch, i);
            buf[(size_t) preWrite] = in;
            int readIdx = preWrite - preDelay;
            if (readIdx < 0) readIdx += preSize;
            float pre = buf[(size_t) readIdx];
            pre = (ch == 0 ? hpL : hpR).processSample (pre);
            buffer.setSample (ch, i, pre);
        }
        preWrite = (preWrite + 1) % preSize;
    }

    // Temporarily push the reverb to 100% wet so our outer dry mix
    // stays authoritative (otherwise the reverb adds its own dry back).
    auto savedDry = params.dryLevel;
    auto savedWet = params.wetLevel;
    params.dryLevel = 0.0f;
    params.wetLevel = 1.0f;
    reverb.setParameters (params);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);

    // Restore so subsequent setMix() calls behave predictably.
    params.dryLevel = savedDry;
    params.wetLevel = savedWet;
    reverb.setParameters (params);

    // Mix wet (in buffer) with original dry.
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        auto* d   = dry.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            wet[i] = d[i] * (1.0f - mix) + wet[i] * mix;
    }
}
