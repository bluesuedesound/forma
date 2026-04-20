#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CaptureBuffer.h"
#include "sampler/SamplePlayer.h"
#include "fx/FXChain.h"
#include "fx/AmbientEngine.h"

// ══════════════════════════════════════════════════════════════════════════
// Per-pad playback mode
// ══════════════════════════════════════════════════════════════════════════

enum class PadMode
{
    Repitch = 0,
    Freeze,
    Reverse,
    Stutter,
    Scatter
};

inline const char* padModeLabel (PadMode m)
{
    switch (m)
    {
        case PadMode::Repitch: return "REPITCH";
        case PadMode::Freeze:  return "FREEZE";
        case PadMode::Reverse: return "REVERSE";
        case PadMode::Stutter: return "STUTTER";
        case PadMode::Scatter: return "SCATTER";
    }
    return "";
}

// ══════════════════════════════════════════════════════════════════════════
// Per-pad state
// ══════════════════════════════════════════════════════════════════════════

struct PadState
{
    float dotX = 0.5f;   // LIGHT (0) → WORN (1)
    float dotY = 0.5f;   // FIXED (0) → BREATHING (1)
    PadMode mode = PadMode::Repitch;

    // Slice region (normalised 0..1 within capture buffer)
    float sliceStart = 0.0f;
    float sliceEnd   = 1.0f;

    // Root note for repitch. Default matches kMidiBase (36 = C2) so pads
    // triggered at their native MIDI note play back at 1.0x pitch. Overridden
    // per-pad in PresaProcessor's constructor to kMidiBase + padIndex.
    int rootNote = 36;
};

// ══════════════════════════════════════════════════════════════════════════
// Capture state
// ══════════════════════════════════════════════════════════════════════════

enum class CaptureState
{
    Idle,
    Armed,
    Recording
};

// ══════════════════════════════════════════════════════════════════════════
// Sampler mode — how a captured sample is mapped across the 16 pads
// ══════════════════════════════════════════════════════════════════════════

enum class SamplerMode
{
    Slice,   // Sample chopped into 16 equal regions; each pad plays its chunk
             // at native pitch.
    Sample   // All 16 pads share the full sample; pitch/speed comes from the
             // MIDI note relative to a single shared root.
};

// In Sample mode, every pad's root resolves to this midi note so pad N's
// trigger note (kMidiBase + N = 36..51) plays the sample at pitch
// 2^((trigger - centerRoot) / 12). 43 places the centre in the middle of
// the pad range so pads below play slower and pads above play faster.
static constexpr int kSampleModeCenterRoot = 43;

// ══════════════════════════════════════════════════════════════════════════
// Processor
// ══════════════════════════════════════════════════════════════════════════

class LoopbackCapture;

class PresaProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumPads = 16;

    // MIDI note range C2..D#3 maps to pads 0..15
    static constexpr int kMidiBase = 36;

    PresaProcessor();
    ~PresaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool   acceptsMidi()  const override { return true; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Capture control (called from editor) ──────────────────────────────
    void armCapture();
    void startCapture();
    void stopCapture();
    CaptureState getCaptureState() const;

    // ── Sampler mode ──────────────────────────────────────────────────────
    void setSamplerMode (SamplerMode m);
    SamplerMode getSamplerMode() const { return samplerMode.load(); }

    // Drag-wire for the waveform handles. The [start, end] pair is in
    // normalised sample units (0..1). In SAMPLE mode every pad is set to
    // this range. In SLICE mode [start, end] is the trim window and the 16
    // pad regions re-divide evenly inside it. Live-safe to call from the UI
    // thread; mirrors into pads[i].sliceStart/End for the waveform overlay.
    void setTrimRange (float startNorm, float endNorm);
    float getTrimStart() const { return trimStart.load(); }
    float getTrimEnd()   const { return trimEnd.load(); }

    // Sample-index bounds of the region the editor's EXPORT button should
    // write out. SAMPLE mode respects the trim window; SLICE mode always
    // exports the full captured buffer so users get every slice in one file.
    int getExportStart() const;
    int getExportEnd()   const;

    // The rate at which the active capture backend is delivering audio
    // (48 kHz for ScreenCaptureKit; 0 if no backend is live). Used by WAV
    // export so the saved file carries the correct rate regardless of what
    // the host has its output device configured to.
    double getCaptureSampleRate() const;

    // Accessor so the editor can hand the sample buffer to a WavAudioFormat
    // writer without reaching through the plain public field.
    SamplePlayer& getSamplePlayer() { return samplePlayer; }

    // ── SAMPLE-mode live params (editor-facing shims) ─────────────────────
    void setPitchSemitones  (int semitones)    { samplePlayer.setPitchSemitones (semitones); }
    void setSpeedMultiplier (float speed)      { samplePlayer.setSpeedMultiplier (speed); }
    void setLooping         (bool shouldLoop)  { samplePlayer.setLooping (shouldLoop); }
    void setReversed        (bool reverse)     { samplePlayer.setReversed (reverse); }

    int   getPitchSemitones()  const { return samplePlayer.getPitchSemitones(); }
    float getSpeedMultiplier() const { return samplePlayer.getSpeedMultiplier(); }
    bool  isLooping()          const { return samplePlayer.isLooping(); }
    bool  isReversed()         const { return samplePlayer.isReversed(); }

    bool isAmbientEngaged() const { return ambientEngine.isEngaged(); }

    // Transient top-bar message. Overrides loopbackDeviceStatus until the
    // 3-second deadline passes, then the regular device status returns.
    void         setStatusMessage (const juce::String& msg);
    juce::String getStatusMessage() const;

    // Shut down loopback capture before JUCE acquires its CriticalSection
    void prepareToStop();

    // ── Pad state (read/write from editor) ─────────────────────────────────
    PadState pads[kNumPads];
    std::atomic<int> selectedPad { 0 };

    // ── Capture state ──────────────────────────────────────────────────────
    std::atomic<int> captureState { static_cast<int> (CaptureState::Idle) };
    CaptureBuffer captureBuffer;

    // ── Sample player ──────────────────────────────────────────────────────
    SamplePlayer samplePlayer;

    // ── Global FX chain (driven by selected pad's StateDot) ────────────────
    FXChain fxChain;

    // ── Ambient feedback engine (SAMPLE mode only, post-SamplePlayer) ──────
    AmbientEngine ambientEngine;

    // ── Loopback capture (primary capture path — bypasses standalone mute) ──
    std::unique_ptr<LoopbackCapture> loopbackCapture;
    juce::String loopbackDeviceStatus { "Initializing..." };
    juce::String captureErrorMessage;

    // ── Sampler mode (atomic so UI toggle from message thread is safe) ─────
    std::atomic<SamplerMode> samplerMode { SamplerMode::Slice };

    // Trim window in normalised [0..1] sample coordinates.
    std::atomic<float> trimStart { 0.0f };
    std::atomic<float> trimEnd   { 1.0f };

    // Transient status overlay (EXPORT success / failure messages, etc.)
    mutable juce::CriticalSection statusMessageLock;
    juce::String  statusMessage;
    std::atomic<juce::int64> statusMessageDeadlineMs { 0 };

    // Flag for editor to detect new sample ready
    std::atomic<bool> newSampleReady { false };

    // ── Global params ──────────────────────────────────────────────────────
    std::atomic<int> rootNote { 60 };

    // ── Audio info for UI ──────────────────────────────────────────────────
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int>    currentBlockSize  { 512 };

    // Trigger a pad from the editor (UI click)
    void triggerPadFromUI (int padIndex);
    void releasePadFromUI (int padIndex);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresaProcessor)
};
