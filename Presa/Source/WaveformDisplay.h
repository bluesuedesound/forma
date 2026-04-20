#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "FormaPalette.h"

class WaveformDisplay : public juce::Component,
                        private juce::Timer
{
public:
    WaveformDisplay();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // Set waveform data to display (will copy internally)
    void setWaveform (const float* samples, int numSamples);
    void clearWaveform();

    // Slice handles (normalised 0..1)
    void setSliceRange (float start, float end);
    float getSliceStart() const { return sliceStart; }
    float getSliceEnd()   const { return sliceEnd; }

    // State-dot values control visual effects
    void setBreathing (float amount);  // 0..1
    void setWorn      (float amount);  // 0..1
    void setScatterRange (float range);  // 0..1

    // ── SAMPLE-mode overlays & interaction ────────────────────────────────
    // Enables vertical-drag pitch editing and the ns-resize cursor.
    void setSampleMode     (bool on);
    void setLoopingVisible (bool on);
    void setReversedVisible(bool on);

    // Called to display a pitch value; fades after 1 second of no updates.
    void setPitchOverlay (int semitones);

    // Callbacks
    std::function<void (float start, float end)> onSliceChanged;

    // Emits the signed semitone delta since the current drag started.
    // Editor combines this with the pre-drag snapshot to set the live pitch.
    std::function<void (int deltaSemitones)> onPitchDelta;
    std::function<void ()> onPitchDragStart;
    std::function<void ()> onPitchDragEnd;

private:
    void timerCallback() override;

    void drawWaveform     (juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawSliceHandles (juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawScatterRange (juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawGrainOverlay (juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawBreathingDeform (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Waveform data (downsampled for display)
    std::vector<float> waveformPeaks;
    int rawSampleCount = 0;

    // Slice
    float sliceStart = 0.0f;
    float sliceEnd   = 1.0f;

    // Effects
    float breathingAmount = 0.0f;
    float wornAmount      = 0.0f;
    float scatterRange    = 0.0f;

    // Drag state
    enum DragTarget { None, StartHandle, EndHandle, Region, PitchVertical };
    DragTarget currentDrag = None;
    float dragOffset = 0.0f;
    int   pitchDragStartY  = 0;
    int   lastEmittedDelta = 0;

    // SAMPLE-mode state
    bool sampleMode      = false;
    bool loopingVisible  = false;
    bool reversedVisible = false;

    // Pitch overlay (fades over time)
    int   pitchOverlaySemis = 0;
    float pitchOverlayAlpha = 0.0f;

    // Breathing animation
    float breathePhase = 0.0f;

    // Grain noise seed
    juce::Random grainRng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
