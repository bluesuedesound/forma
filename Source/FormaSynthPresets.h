#pragma once
#include "FormaSynth.h"

namespace FormaSynthPresets {

using P = FormaVoiceParams;
using W = P::Waveform;
using T = P::LfoTarget;
using L = P::LfoWave;

// ── CHORD PRESETS ─────────────────────────────────────────────

inline P Keys() {
    P p;
    p.vco1Wave = W::Sine;  p.vco1Level = 0.6f;
    p.vco2Wave = W::Sine;  p.vco2Level = 0.4f;
    p.vco2Detune = 0.07f;
    p.filterCutoff = 0.85f; p.filterRes = 0.1f; p.filterEgAmount = 0.3f;
    p.filterAttack = 0.005f; p.filterDecay = 0.4f; p.filterSustain = 0.2f; p.filterRelease = 0.3f;
    p.ampAttack = 0.008f; p.ampDecay = 0.12f; p.ampSustain = 0.75f; p.ampRelease = 0.5f;
    p.saturation = 1.6f; p.voiceGain = 0.16f;
    return p;
}

inline P Felt() {
    P p;
    p.vco1Wave = W::Square; p.vco1Level = 0.7f;
    p.vco2Wave = W::Sine;   p.vco2Level = 0.3f;
    p.vco2Detune = 0.05f;
    p.filterCutoff = 0.45f; p.filterRes = 0.05f; p.filterEgAmount = 0.25f;
    p.filterAttack = 0.003f; p.filterDecay = 0.2f; p.filterSustain = 0.0f; p.filterRelease = 0.15f;
    p.ampAttack = 0.005f; p.ampDecay = 0.08f; p.ampSustain = 0.55f; p.ampRelease = 0.12f;
    p.saturation = 1.3f; p.voiceGain = 0.18f;
    return p;
}

inline P Glass() {
    P p;
    p.vco1Wave = W::Sine; p.vco1Level = 0.8f;
    p.vco2Wave = W::Sine; p.vco2Level = 0.2f;
    p.vco2Detune = 0.12f; p.vco2Octave = 1.0f;
    p.filterCutoff = 0.9f; p.filterRes = 0.2f; p.filterEgAmount = 0.1f;
    p.filterAttack = 0.3f; p.filterDecay = 0.5f; p.filterSustain = 0.7f; p.filterRelease = 0.8f;
    p.ampAttack = 0.35f; p.ampDecay = 0.2f; p.ampSustain = 0.8f; p.ampRelease = 1.2f;
    p.lfoTarget = T::Pitch; p.lfoWave = L::Sine; p.lfoRate = 4.5f; p.lfoAmount = 0.015f; p.lfoDelay = 0.4f;
    p.saturation = 1.0f; p.voiceGain = 0.15f;
    return p;
}

inline P Tape() {
    P p;
    p.vco1Wave = W::Saw; p.vco1Level = 0.55f;
    p.vco2Wave = W::Saw; p.vco2Level = 0.45f;
    p.vco2Detune = 0.15f;
    p.filterCutoff = 0.6f; p.filterRes = 0.15f; p.filterEgAmount = 0.35f;
    p.filterAttack = 0.008f; p.filterDecay = 0.5f; p.filterSustain = 0.3f; p.filterRelease = 0.4f;
    p.ampAttack = 0.012f; p.ampDecay = 0.15f; p.ampSustain = 0.72f; p.ampRelease = 0.6f;
    p.lfoTarget = T::Pitch; p.lfoWave = L::Sine; p.lfoRate = 0.4f; p.lfoAmount = 0.008f;
    p.saturation = 2.2f; p.voiceGain = 0.14f;
    return p;
}

inline P PadPreset() {
    P p;
    p.vco1Wave = W::Saw; p.vco1Level = 0.6f;
    p.vco2Wave = W::Saw; p.vco2Level = 0.4f;
    p.vco2Detune = 0.18f; p.vco2Octave = -1.0f;
    p.filterCutoff = 0.35f; p.filterRes = 0.1f; p.filterEgAmount = 0.55f;
    p.filterAttack = 0.5f; p.filterDecay = 0.8f; p.filterSustain = 0.6f; p.filterRelease = 1.0f;
    p.ampAttack = 0.45f; p.ampDecay = 0.3f; p.ampSustain = 0.85f; p.ampRelease = 1.8f;
    p.lfoTarget = T::Filter; p.lfoWave = L::Sine; p.lfoRate = 0.3f; p.lfoAmount = 0.08f;
    p.saturation = 1.5f; p.voiceGain = 0.13f;
    return p;
}

// ── BASS PRESETS ──────────────────────────────────────────────

inline P Sub() {
    P p;
    p.vco1Wave = W::Sine;   p.vco1Level = 0.7f;
    p.vco2Wave = W::Square; p.vco2Level = 0.3f;
    p.vco2Octave = -1.0f;
    p.filterCutoff = 0.3f; p.filterRes = 0.05f; p.filterEgAmount = 0.15f;
    p.filterAttack = 0.003f; p.filterDecay = 0.15f; p.filterSustain = 0.0f; p.filterRelease = 0.1f;
    p.ampAttack = 0.004f; p.ampDecay = 0.1f; p.ampSustain = 0.88f; p.ampRelease = 0.6f;
    p.saturation = 1.8f; p.voiceGain = 0.25f;
    return p;
}

inline P Rubber() {
    P p;
    p.vco1Wave = W::Square; p.vco1Level = 0.65f;
    p.vco2Wave = W::Saw;    p.vco2Level = 0.35f;
    p.vco2Detune = 0.1f;
    p.filterCutoff = 0.25f; p.filterRes = 0.3f; p.filterEgAmount = 0.7f;
    p.filterAttack = 0.002f; p.filterDecay = 0.18f; p.filterSustain = 0.0f; p.filterRelease = 0.1f;
    p.ampAttack = 0.002f; p.ampDecay = 0.15f; p.ampSustain = 0.65f; p.ampRelease = 0.25f;
    p.saturation = 2.0f; p.voiceGain = 0.22f;
    return p;
}

inline P Vintage() {
    P p;
    p.vco1Wave = W::Saw;    p.vco1Level = 0.7f;
    p.vco2Wave = W::Square; p.vco2Level = 0.3f;
    p.vco2Detune = 0.06f;
    p.filterCutoff = 0.5f; p.filterRes = 0.2f; p.filterEgAmount = 0.2f;
    p.filterAttack = 0.005f; p.filterDecay = 0.3f; p.filterSustain = 0.3f; p.filterRelease = 0.3f;
    p.ampAttack = 0.006f; p.ampDecay = 0.12f; p.ampSustain = 0.78f; p.ampRelease = 0.45f;
    p.saturation = 3.0f; p.voiceGain = 0.20f;
    return p;
}

// ── ARP PRESETS ──────────────────────────────────────────────

inline P Pluck() {
    P p;
    p.vco1Wave = W::Sine;   p.vco1Level = 0.6f;
    p.vco2Wave = W::Square; p.vco2Level = 0.4f;
    p.vco2Detune = 0.04f;
    p.filterCutoff = 0.7f; p.filterRes = 0.15f; p.filterEgAmount = 0.5f;
    p.filterAttack = 0.001f; p.filterDecay = 0.12f; p.filterSustain = 0.0f; p.filterRelease = 0.08f;
    p.ampAttack = 0.001f; p.ampDecay = 0.06f; p.ampSustain = 0.45f; p.ampRelease = 0.15f;
    p.saturation = 1.5f; p.voiceGain = 0.15f;
    return p;
}

inline P Bell() {
    P p;
    p.vco1Wave = W::Sine; p.vco1Level = 0.5f;
    p.vco2Wave = W::Sine; p.vco2Level = 0.5f;
    p.vco2Octave = 2.0f; p.vco2Detune = 0.3f;
    p.filterCutoff = 0.95f; p.filterRes = 0.0f; p.filterEgAmount = 0.0f;
    p.ampAttack = 0.001f; p.ampDecay = 0.8f; p.ampSustain = 0.1f; p.ampRelease = 1.5f;
    p.saturation = 1.1f; p.voiceGain = 0.12f;
    return p;
}

inline P Mallet() {
    P p;
    p.vco1Wave = W::Sine; p.vco1Level = 0.7f;
    p.vco2Wave = W::Sine; p.vco2Level = 0.3f;
    p.vco2Octave = 1.0f;
    p.filterCutoff = 0.8f; p.filterRes = 0.05f; p.filterEgAmount = 0.2f;
    p.filterAttack = 0.001f; p.filterDecay = 0.15f; p.filterSustain = 0.0f; p.filterRelease = 0.1f;
    p.ampAttack = 0.003f; p.ampDecay = 0.25f; p.ampSustain = 0.3f; p.ampRelease = 0.4f;
    p.saturation = 1.2f; p.voiceGain = 0.14f;
    return p;
}

} // namespace FormaSynthPresets
