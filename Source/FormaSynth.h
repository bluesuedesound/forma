#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// ═══════════════════════════════════════════════════════════════════════════
// Voice parameter struct — defines a complete synthesizer patch
// ═══════════════════════════════════════════════════════════════════════════

struct FormaVoiceParams
{
    enum class Waveform  { Sine, Saw, Square };
    enum class LfoTarget { None, Pitch, Filter, Amplitude };
    enum class LfoWave   { Sine, Square, Triangle };

    // VCO 1
    Waveform vco1Wave  = Waveform::Sine;
    float vco1Level    = 0.7f;

    // VCO 2
    Waveform vco2Wave  = Waveform::Sine;
    float vco2Level    = 0.3f;
    float vco2Detune   = 0.0f;   // semitones (fractional for cents)
    float vco2Octave   = 0.0f;   // -2 to +2

    // Filter
    float filterCutoff   = 1.0f;   // 0-1 (maps to 80-18000Hz)
    float filterRes      = 0.0f;   // 0-1
    float filterEgAmount = 0.0f;   // 0-1
    float filterAttack   = 0.01f;
    float filterDecay    = 0.3f;
    float filterSustain  = 0.0f;
    float filterRelease  = 0.2f;

    // Amp EG
    float ampAttack  = 0.01f;
    float ampDecay   = 0.1f;
    float ampSustain = 0.8f;
    float ampRelease = 0.4f;

    // LFO
    LfoTarget lfoTarget = LfoTarget::None;
    LfoWave   lfoWave   = LfoWave::Sine;
    float lfoRate   = 5.0f;
    float lfoAmount = 0.0f;
    float lfoDelay  = 0.0f;

    // Master
    float voiceGain  = 0.18f;
    float saturation = 1.4f;
};

// ═══════════════════════════════════════════════════════════════════════════
// FormaVoice — two-oscillator subtractive synthesizer voice
// ═══════════════════════════════════════════════════════════════════════════

class FormaVoice : public juce::SynthesiserVoice
{
public:
    FormaVoice() = default;

    FormaVoiceParams params;

    bool canPlaySound (juce::SynthesiserSound* s) override { return s != nullptr; }

    void startNote (int midiNote, float vel, juce::SynthesiserSound*, int) override
    {
        velocity = vel;
        double sr = getSampleRate();
        targetFreq = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        currentFreq = targetFreq;

        vco1Phase = 0.0;
        vco2Phase = 0.0;
        filterState1 = 0.0f;
        filterState2 = 0.0f;

        ampEG.setSampleRate (sr);
        ampEG.setParameters ({ params.ampAttack, params.ampDecay,
                               params.ampSustain, params.ampRelease });
        ampEG.noteOn();

        filterEG.setSampleRate (sr);
        filterEG.setParameters ({ params.filterAttack, params.filterDecay,
                                   params.filterSustain, params.filterRelease });
        filterEG.noteOn();

        lfoPhase = 0.0;
        lfoDelayCounter = params.lfoDelay;
        lfoDelayEnv = 0.0f;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) { ampEG.noteOff(); filterEG.noteOff(); }
        else { ampEG.reset(); filterEG.reset(); clearCurrentNote(); }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void prepareToPlay (double sr, int)
    {
        ampEG.setSampleRate (sr);
        filterEG.setSampleRate (sr);
    }

    void renderNextBlock (juce::AudioBuffer<float>& buf, int startSample, int numSamples) override
    {
        if (! isVoiceActive()) return;

        double sr = getSampleRate();
        if (sr <= 0) return;
        double twoPi = juce::MathConstants<double>::twoPi;

        for (int i = startSample; i < startSample + numSamples; ++i)
        {
            // ── LFO ──
            if (lfoDelayCounter > 0.0)
            {
                lfoDelayCounter -= 1.0 / sr;
                lfoDelayEnv = 0.0f;
            }
            else
                lfoDelayEnv = juce::jmin (1.0f, lfoDelayEnv + (float)(1.0 / (sr * 0.1)));

            lfoPhase += twoPi * params.lfoRate / sr;
            if (lfoPhase >= twoPi) lfoPhase -= twoPi;

            float lfo = 0.0f;
            switch (params.lfoWave)
            {
                case FormaVoiceParams::LfoWave::Sine:
                    lfo = (float) std::sin (lfoPhase); break;
                case FormaVoiceParams::LfoWave::Square:
                    lfo = lfoPhase < juce::MathConstants<double>::pi ? 1.0f : -1.0f; break;
                case FormaVoiceParams::LfoWave::Triangle:
                    lfo = (float)(lfoPhase < juce::MathConstants<double>::pi
                        ? (lfoPhase / juce::MathConstants<double>::pi * 2.0 - 1.0)
                        : (3.0 - lfoPhase / juce::MathConstants<double>::pi * 2.0));
                    break;
            }
            lfo *= params.lfoAmount * lfoDelayEnv;

            // ── VCO frequencies ──
            double freq1 = currentFreq;
            if (params.lfoTarget == FormaVoiceParams::LfoTarget::Pitch)
                freq1 *= std::pow (2.0, (double) lfo * 2.0 / 12.0);

            double freq2 = freq1 * std::pow (2.0, (double) params.vco2Detune / 12.0)
                                 * std::pow (2.0, (double) params.vco2Octave);

            // ── VCO waveforms ──
            vco1Phase += twoPi * freq1 / sr;
            if (vco1Phase >= twoPi) vco1Phase -= twoPi;
            float vco1 = generateWave (params.vco1Wave, vco1Phase);

            vco2Phase += twoPi * freq2 / sr;
            if (vco2Phase >= twoPi) vco2Phase -= twoPi;
            float vco2 = generateWave (params.vco2Wave, vco2Phase);

            float mixed = vco1 * params.vco1Level + vco2 * params.vco2Level;

            // ── Filter ──
            float fegVal = filterEG.getNextSample();
            float cutNorm = params.filterCutoff + fegVal * params.filterEgAmount;
            if (params.lfoTarget == FormaVoiceParams::LfoTarget::Filter)
                cutNorm += lfo * 0.3f;
            cutNorm = juce::jlimit (0.0f, 1.0f, cutNorm);

            float cutHz = 80.0f * std::pow (225.0f, cutNorm);
            cutHz = juce::jlimit (80.0f, 18000.0f, cutHz);

            float w = 2.0f * (float) sr;
            float tpc = (float)(twoPi) * cutHz;
            float lpC = tpc / (w + tpc);

            float fb = params.filterRes * 3.5f * (1.0f - 0.15f * lpC * lpC);
            float inp = mixed - filterState2 * fb;
            filterState1 += lpC * (inp - filterState1);
            filterState2 += lpC * (filterState1 - filterState2);
            float filtered = filterState2;

            // ── Amp EG ──
            float ampEnv = ampEG.getNextSample();
            if (! ampEG.isActive()) { clearCurrentNote(); return; }

            float ampMod = 1.0f;
            if (params.lfoTarget == FormaVoiceParams::LfoTarget::Amplitude)
                ampMod = 1.0f + lfo * 0.5f;

            float out = filtered * ampEnv * ampMod;

            // ── Saturation ──
            if (params.saturation > 1.001f)
            {
                float driven = out * params.saturation;
                out = std::tanh (driven) / std::tanh (params.saturation);
            }

            out *= params.voiceGain * velocity;

            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.addSample (ch, i, out);
        }
    }

private:
    float velocity = 0.0f;
    double currentFreq = 440.0, targetFreq = 440.0;
    double vco1Phase = 0.0, vco2Phase = 0.0;
    juce::ADSR ampEG, filterEG;
    float filterState1 = 0.0f, filterState2 = 0.0f;
    double lfoPhase = 0.0;
    float lfoDelayEnv = 0.0f;
    double lfoDelayCounter = 0.0;

    static float generateWave (FormaVoiceParams::Waveform w, double phase)
    {
        switch (w)
        {
            case FormaVoiceParams::Waveform::Sine:
                return (float) std::sin (phase);
            case FormaVoiceParams::Waveform::Saw:
                return (float)(phase / juce::MathConstants<double>::twoPi * 2.0 - 1.0);
            case FormaVoiceParams::Waveform::Square:
                return phase < juce::MathConstants<double>::pi ? 1.0f : -1.0f;
        }
        return 0.0f;
    }
};

// ═══════════════════════════════════════════════════════════════════════════

class FormaSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};
