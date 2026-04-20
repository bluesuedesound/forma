#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Evolving feedback processor for SAMPLE mode. Takes the output of
// SamplePlayer and builds a living ambient texture via three slowly
// evolving parameters driven by the selected pad's StateDot:
//
//   dotX (DRIFT)    — rate/depth of pitch & speed drift, feedback gain
//   dotY (DISSOLVE) — rate of timbral darkening, feedback gain
//
// Architecture:
//   input → (read from feedback buffer with pitch/speed-modulated
//           fractional delay) → one-pole LPF → scaled feedback gain
//        → summed with input → output (also written back into buffer)
//
// Silent when the dot sits in the STILL/FOCUSED quadrant (both < 0.2).
class AmbientEngine
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    // numSamples of buffer processed in-place. dot values are normalised 0..1.
    void processBlock (juce::AudioBuffer<float>& buffer, float dotX, float dotY);

    // True whenever the current feedback gain is audibly active.
    // UI uses this to light the outer glow ring on the StateDot.
    bool isEngaged() const noexcept { return feedbackGain > 0.15f; }

private:
    // Circular feedback buffer (mono interleaved per channel).
    std::vector<float> bufL, bufR;
    int   bufSize = 0;
    int   writePos = 0;

    // Base delay at which the feedback tap is read. Roughly 500 ms —
    // long enough for pitch/speed drift to smear successive iterations
    // into a wash but short enough to not feel like a delay line.
    float baseDelaySamples = 0.0f;

    // Three slow LFO phases (radians). Only pitch & speed drive
    // delay-time modulation; the third wobbles the cutoff slightly
    // around its drift target so the darkening isn't monotonic.
    float pitchPhase  = 0.0f;
    float speedPhase  = 0.0f;
    float timbrePhase = 0.0f;

    // One-pole LPF state per channel. Cutoff drifts down over time
    // with dotY and resets to 18 kHz when the dot returns to FOCUSED.
    float lpStateL = 0.0f;
    float lpStateR = 0.0f;
    float currentCutoff = 18000.0f;

    // Smoothed feedback gain target so changes don't click.
    float feedbackGain = 0.0f;

    double sampleRate = 44100.0;

    // Linear-interpolated delay read.
    float readDelayed (int channel, float delaySamples) const noexcept;
};
