#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

class SamplePlayer
{
public:
    static constexpr int kNumPads  = 16;
    static constexpr int kMaxVoices = 16;

    // How a loaded sample is carved up across the 16 pads.
    enum class SliceLayout
    {
        Even,        // Default: 16 equal chunks, one per pad.
        FullRange    // Every pad covers the whole sample (for chromatic mode).
    };

    SamplePlayer();

    void prepare (double sampleRate, int blockSize);

    // Load a new captured sample. Slices according to the current SliceLayout.
    void setSample (juce::AudioBuffer<float>&& newSample, int sampleRateOfSample);

    // Change the slice layout. If a sample is loaded, per-pad regions are
    // recomputed immediately so a mode toggle takes effect without a
    // re-capture. Kills in-flight voices to avoid out-of-range playheads.
    void setSliceLayout (SliceLayout layout);
    SliceLayout getSliceLayout() const { return sliceLayout; }

    bool hasSample() const { return sampleBuffer.getNumSamples() > 0; }
    int  getSampleLength() const { return sampleLength; }

    // Returns the sample buffer for waveform display
    const juce::AudioBuffer<float>& getSampleBuffer() const { return sampleBuffer; }

    // Trigger / release
    void noteOn  (int padIndex, int midiNote, int rootNote);
    void noteOff (int padIndex);

    // Render audio
    void processBlock (juce::AudioBuffer<float>& output, int numSamples);

    // Per-pad slice boundaries (sample indices into sampleBuffer)
    struct SliceRegion
    {
        int start = 0;
        int end   = 0;
    };
    SliceRegion getSlice (int padIndex) const;

    // Override a pad's slice region live (called when the user drags the
    // waveform handles). Clamps to [0, sampleLength]. Any voice on the pad
    // whose playhead now sits outside the new region is cut.
    void setPadSlice (int padIndex, int startSample, int endSample);

    // ── SAMPLE-mode live params ──────────────────────────────────────────
    // Pitch snaps to integer semitones (-24..+24). Speed is continuous but
    // the editor exposes a discrete value set (see PluginEditor).
    void  setPitchSemitones  (int semitones);
    void  setSpeedMultiplier (float speed);
    void  setLooping  (bool shouldLoop) { looping.store (shouldLoop); }
    void  setReversed (bool reverse)    { reversed.store (reverse); }

    int   getPitchSemitones()  const { return pitchOffsetSemitones.load(); }
    float getSpeedMultiplier() const { return speedMultiplier.load(); }
    bool  isLooping()          const { return looping.load(); }
    bool  isReversed()         const { return reversed.load(); }

private:
    struct Voice
    {
        int    padIndex   = -1;
        double playhead   = 0.0;
        // Pitch ratio baked in at noteOn from (midi - root)/12 + srcSR/hostSR.
        // Live pitch/speed multipliers are layered on top per-sample.
        double basePitchRatio = 1.0;
        bool   active     = false;
        bool   releasing  = false;

        // ADSR
        float envLevel    = 0.0f;
        float attackRate  = 0.0f;
        float releaseRate = 0.0f;
        enum class EnvStage { Attack, Sustain, Release, Off } envStage = EnvStage::Off;
    };

    Voice voices[kMaxVoices];
    juce::AudioBuffer<float> sampleBuffer;
    int sampleLength = 0;
    double hostSampleRate = 44100.0;
    int sampleOriginalRate = 44100;

    // Slice grid: per-pad regions computed from sliceLayout
    SliceRegion slices[kNumPads];
    SliceLayout sliceLayout = SliceLayout::Even;
    void recomputeSlices();

    // Find a free voice (or steal oldest)
    int findFreeVoice (int padIndex) const;

    // Debug: log first playback
    bool hasLoggedFirstPlayback = false;

    // ── Live-adjustable params (touched from UI thread, read from audio) ──
    std::atomic<int>   pitchOffsetSemitones { 0 };
    std::atomic<float> speedMultiplier      { 1.0f };
    std::atomic<bool>  looping              { false };
    std::atomic<bool>  reversed             { false };

    // Loop crossfade length in samples — enough to hide the seam without
    // audibly shortening short slices.
    static constexpr int kLoopXfadeSamples = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePlayer)
};
