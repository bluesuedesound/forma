#include "PluginEditor.h"
#include "capture/LoopbackCapture.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_formats/juce_audio_formats.h>
#endif

// ══════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════

juce::Font PresaEditor::mono (float h) const
{
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain);
}

juce::String PresaEditor::noteName (int midiNote)
{
    static const char* names[] = { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };
    int oct = (midiNote / 12) - 1;
    return juce::String (names[midiNote % 12]) + juce::String (oct);
}

juce::Colour PresaEditor::getCaptureStateColour() const
{
    auto state = static_cast<CaptureState> (proc.captureState.load());
    switch (state)
    {
        case CaptureState::Armed:
            return Palette::LINK_ACTIVE();

        case CaptureState::Recording:
        {
            // Fast pulse (≈4 Hz)
            float alpha = 0.55f + 0.45f * std::sin (captureDotPhase * 8.0f);
            return Palette::ACCENT().withAlpha (alpha);
        }

        default:
        {
            // Slow breathing fade (≈0.7 Hz)
            float alpha = 0.3f + 0.25f * std::sin (captureDotPhase);
            return Palette::TXT_DIM().withAlpha (alpha);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Constructor
// ══════════════════════════════════════════════════════════════════════════

PresaEditor::PresaEditor (PresaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (kWidth, kHeight);
    setOpaque (true);
    setLookAndFeel (&lnf);

    // Wire up pad states to child components
    waveformDisplay.setSliceRange (0.0f, 1.0f);
    stateDotField.setPadStates (proc.pads, PresaProcessor::kNumPads);
    padGrid.setPadStates (proc.pads, PresaProcessor::kNumPads);

    int sel = proc.selectedPad.load();
    stateDotField.setSelectedPad (sel);
    padGrid.setSelectedPad (sel);

    // Pad selection callback
    padGrid.onPadSelected = [this] (int idx)
    {
        proc.selectedPad.store (idx);
        stateDotField.setSelectedPad (idx);

        // Update waveform effects from newly selected pad. Slice range now
        // reflects the processor-level trim window (shared across pads), not
        // a per-pad region — the waveform represents the trim, the pad grid
        // represents per-pad state.
        auto& pad = proc.pads[idx];
        waveformDisplay.setBreathing (pad.dotY);
        waveformDisplay.setWorn (pad.dotX);
        waveformDisplay.setScatterRange (pad.dotY * 0.4f);
        waveformDisplay.setSliceRange (proc.getTrimStart(), proc.getTrimEnd());

        // Trigger playback on click
        proc.triggerPadFromUI (idx);
    };

    // Right-click to cycle mode
    padGrid.onPadRightClick = [this] (int idx)
    {
        auto& pad = proc.pads[idx];
        int m = (static_cast<int> (pad.mode) + 1) % 5;
        pad.mode = static_cast<PadMode> (m);
        padGrid.repaint();
    };

    // Dot drag callback
    stateDotField.onDotMoved = [this] (int idx, float x, float y)
    {
        if (idx == proc.selectedPad.load())
        {
            waveformDisplay.setBreathing (y);
            waveformDisplay.setWorn (x);
            waveformDisplay.setScatterRange (y * 0.4f);
        }
    };

    // Waveform handle drag: update the processor's trim window live.
    // In SAMPLE mode this retargets all 16 pads to the same region; in SLICE
    // mode the window is subdivided into 16 pad slices inside.
    waveformDisplay.onSliceChanged = [this] (float start, float end)
    {
        proc.setTrimRange (start, end);
    };

    // Vertical drag on the waveform = pitch (SAMPLE mode only). Editor
    // remembers the pre-drag value and applies deltas relative to it so the
    // final value is independent of how the drag crosses frames.
    waveformDisplay.onPitchDragStart = [this]
    {
        pitchAtDragStart = proc.getPitchSemitones();
    };
    waveformDisplay.onPitchDelta = [this] (int delta)
    {
        const int target = juce::jlimit (-24, 24, pitchAtDragStart + delta);
        proc.setPitchSemitones (target);
        waveformDisplay.setPitchOverlay (target);
    };

    // Add child components
    addAndMakeVisible (waveformDisplay);
    addAndMakeVisible (stateDotField);
    addAndMakeVisible (padGrid);

    // Capture bar buttons
    for (auto* btn : { &armBtn, &captureBtn, &stopBtn, &modeBtn, &autoSliceBtn })
    {
        btn->setClickingTogglesState (false);
        addAndMakeVisible (btn);
    }

    // SAMPLE-mode toggles. Visible only when the processor is in SAMPLE mode
    // (gated in resized()); styled as ACCENT pills when active, DARK when not.
    for (auto* btn : { &loopBtn, &reverseBtn })
    {
        btn->setClickingTogglesState (false);
        addChildComponent (btn);
    }

    loopBtn.onClick = [this]
    {
        proc.setLooping (! proc.isLooping());
        refreshSampleControlButtons();
        waveformDisplay.setLoopingVisible (proc.isLooping());
    };
    reverseBtn.onClick = [this]
    {
        proc.setReversed (! proc.isReversed());
        refreshSampleControlButtons();
        waveformDisplay.setReversedVisible (proc.isReversed());
    };
    refreshSampleControlButtons();

    // Mode toggle — flip between Slice and Sample, refresh label + layout.
    modeBtn.onClick = [this]
    {
        auto current = proc.getSamplerMode();
        proc.setSamplerMode (current == SamplerMode::Slice
                             ? SamplerMode::Sample
                             : SamplerMode::Slice);
        refreshModeButton();

        const bool sampleMode = (proc.getSamplerMode() == SamplerMode::Sample);
        waveformDisplay.setSampleMode (sampleMode);
        waveformDisplay.setLoopingVisible  (sampleMode && proc.isLooping());
        waveformDisplay.setReversedVisible (sampleMode && proc.isReversed());
        stateDotField.setAxisLabels (
            sampleMode ? "STILL"    : "LIGHT",
            sampleMode ? "DRIFT"    : "WORN",
            sampleMode ? "FOCUSED"  : "FIXED",
            sampleMode ? "DISSOLVE" : "BREATHING");

        resized();  // layout visibility depends on the new mode
        repaint();
    };
    refreshModeButton();

    // Seed the waveform and dot-field with the current mode's state so a
    // saved-state restore lands in SAMPLE mode with the right labels.
    const bool sampleModeInit = (proc.getSamplerMode() == SamplerMode::Sample);
    waveformDisplay.setSampleMode (sampleModeInit);
    waveformDisplay.setLoopingVisible  (sampleModeInit && proc.isLooping());
    waveformDisplay.setReversedVisible (sampleModeInit && proc.isReversed());
    stateDotField.setAxisLabels (
        sampleModeInit ? "STILL"    : "LIGHT",
        sampleModeInit ? "DRIFT"    : "WORN",
        sampleModeInit ? "FOCUSED"  : "FIXED",
        sampleModeInit ? "DISSOLVE" : "BREATHING");

   #if JucePlugin_Build_Standalone
    exportBtn.setClickingTogglesState (false);
    addAndMakeVisible (exportBtn);
    exportBtn.onClick = [this] { exportSample(); };
    exportBtn.setEnabled (proc.getSamplePlayer().hasSample());
    exportBtn.setColour (juce::TextButton::buttonColourId, Palette::DARK());
    exportBtn.setColour (juce::TextButton::textColourOffId, Palette::TXT_MID());
   #endif

    armBtn.onClick = [this]
    {
        if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
        { fprintf (f, "ARM CLICKED — captureState=%d\n", proc.captureState.load()); fclose (f); }
        proc.armCapture();
    };
    captureBtn.onClick = [this]
    {
        if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
        { fprintf (f, "CAPTURE CLICKED — captureState=%d\n", proc.captureState.load()); fclose (f); }
        proc.startCapture();
    };
    stopBtn.onClick = [this]
    {
        if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
        { fprintf (f, "STOP CLICKED — captureState=%d\n", proc.captureState.load()); fclose (f); }
        proc.stopCapture();
    };

    rootNoteDisplay = proc.rootNote.load();

    startTimerHz (60);
}

PresaEditor::~PresaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PresaEditor::refreshModeButton()
{
    const bool isSample = (proc.getSamplerMode() == SamplerMode::Sample);
    modeBtn.setButtonText (isSample ? "SAMPLE" : "SLICE");

    // Both states use the accent pill — the label carries the meaning.
    modeBtn.setColour (juce::TextButton::buttonColourId,   Palette::ACCENT());
    modeBtn.setColour (juce::TextButton::textColourOffId,  Palette::TXT_HI());
}

void PresaEditor::refreshSampleControlButtons()
{
    auto style = [] (juce::TextButton& b, bool active)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     active ? Palette::ACCENT() : Palette::DARK());
        b.setColour (juce::TextButton::textColourOffId,
                     active ? Palette::TXT_HI() : Palette::TXT_MID());
        b.repaint();
    };
    style (loopBtn,    proc.isLooping());
    style (reverseBtn, proc.isReversed());
}

// Discrete speed ladder. Small enough that dragging a few pixels produces
// a recognisable jump rather than a smooth sweep — matches the design brief.
static const float kSpeedSteps[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
static constexpr int kSpeedStepCount = (int) (sizeof (kSpeedSteps) / sizeof (kSpeedSteps[0]));

int PresaEditor::speedIndex (float speed)
{
    int best = 3;   // default → 1.0x
    float bestDist = std::abs (speed - kSpeedSteps[best]);
    for (int i = 0; i < kSpeedStepCount; ++i)
    {
        float d = std::abs (speed - kSpeedSteps[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

float PresaEditor::speedFromIndex (int idx)
{
    return kSpeedSteps[juce::jlimit (0, kSpeedStepCount - 1, idx)];
}

float PresaEditor::snapSpeed (float speed)
{
    return kSpeedSteps[speedIndex (speed)];
}

// ══════════════════════════════════════════════════════════════════════════
// Timer
// ══════════════════════════════════════════════════════════════════════════

void PresaEditor::timerCallback()
{
    float dt = 1.0f / 60.0f;

    // Advance capture dot animation phase
    captureDotPhase += dt * juce::MathConstants<float>::twoPi * 0.7f;
    if (captureDotPhase > juce::MathConstants<float>::twoPi * 100.0f)
        captureDotPhase -= juce::MathConstants<float>::twoPi * 100.0f;

   #if JucePlugin_Build_Standalone
    // Keep EXPORT enabled-state synced with whether a sample is loaded.
    const bool canExport = (proc.getSamplePlayer().hasSample());
    if (exportBtn.isEnabled() != canExport)
        exportBtn.setEnabled (canExport);

    // Revert the flash colour 500 ms after the click.
    if (exportFlashUntilMs != 0)
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (now >= exportFlashUntilMs)
        {
            exportFlashUntilMs = 0;
            exportBtn.setColour (juce::TextButton::buttonColourId, Palette::DARK());
            exportBtn.setColour (juce::TextButton::textColourOffId, Palette::TXT_MID());
            exportBtn.repaint();
        }
    }
   #endif

    // Check for new sample from capture
    if (proc.newSampleReady.exchange (false))
    {
        auto& buf = proc.samplePlayer.getSampleBuffer();
        if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
        {
            fprintf (f, "Editor: newSampleReady EDGE — buf.numSamples=%d numChannels=%d\n",
                     buf.getNumSamples(), buf.getNumChannels());
            fclose (f);
        }
        if (buf.getNumSamples() > 0)
        {
            waveformDisplay.setWaveform (buf.getReadPointer (0), buf.getNumSamples());

            // Waveform handles now mirror the trim window, not a per-pad
            // slice, so the display is consistent across pad selection and
            // the drag callback can treat them as the single source of truth.
            waveformDisplay.setSliceRange (proc.getTrimStart(), proc.getTrimEnd());
        }
        padGrid.repaint();
    }

    // Sync async capture status into the field the top-bar paint reads.
    if (proc.loopbackCapture != nullptr)
    {
        auto s = proc.loopbackCapture->getDeviceStatus();
        if (s != proc.loopbackDeviceStatus)
            proc.loopbackDeviceStatus = s;
    }

    // Sync effects from current pad state
    int sel = proc.selectedPad.load();
    auto& pad = proc.pads[sel];
    waveformDisplay.setBreathing (pad.dotY);
    waveformDisplay.setWorn (pad.dotX);
    waveformDisplay.setScatterRange (pad.dotY * 0.4f);

    // AmbientEngine engagement ring — only meaningful in SAMPLE mode.
    stateDotField.setAmbientEngaged (
        (proc.getSamplerMode() == SamplerMode::Sample)
        && proc.isAmbientEngaged());

    repaint();  // top bar + status bar
}

// ══════════════════════════════════════════════════════════════════════════
// Layout
// ══════════════════════════════════════════════════════════════════════════

void PresaEditor::resized()
{
    auto bounds = getLocalBounds();

    // Top bar — painted only (no child components)
    int y = kTopBarH;

    // Waveform zone
    waveformDisplay.setBounds (0, y, kWidth, kWaveformH);
    y += kWaveformH;

    // Capture bar — buttons positioned here
    int capY = y;
    int btnW = 60;
    int btnH = 22;
    int btnY = capY + (kCaptureBarH - btnH) / 2;
    int bx = 12;

    armBtn.setBounds (bx, btnY, btnW, btnH);      bx += btnW + 6;
    captureBtn.setBounds (bx, btnY, btnW + 10, btnH); bx += btnW + 16;
    stopBtn.setBounds (bx, btnY, btnW, btnH);     bx += btnW + 10;

    // SLICE/SAMPLE toggle sits between STOP and the centre root-note block.
    modeBtn.setBounds (bx, btnY, 72, btnH);

    // Root note area (centre of capture bar)
    rootNoteRect   = { kWidth / 2 - 30, btnY, 60, btnH };
    rootNoteDnRect = { rootNoteRect.getX() - 16, btnY, 14, btnH };
    rootNoteUpRect = { rootNoteRect.getRight() + 2, btnY, 14, btnH };

    // Auto-slice is only meaningful in SLICE mode — hide in SAMPLE mode
    // where every pad already covers the whole sample.
    const bool sliceMode = (proc.getSamplerMode() == SamplerMode::Slice);
    autoSliceBtn.setVisible (sliceMode);

    // EXPORT sits at the far right. AUTO-SLICE sits to its left when visible;
    // when hidden (SAMPLE mode), EXPORT takes the same far-right position.
    int rightEdge = kWidth - 12;
   #if JucePlugin_Build_Standalone
    const int exportW = 72;
    exportBtn.setBounds (rightEdge - exportW, btnY, exportW, btnH);
    rightEdge -= exportW + 6;
   #endif
    if (sliceMode)
        autoSliceBtn.setBounds (rightEdge - 88, btnY, 88, btnH);
    else
        autoSliceBtn.setBounds (rightEdge - 88, btnY, 88, btnH);  // offscreen when invisible

    y += kCaptureBarH;

    // Lower zone — mode-dependent.
    int lowerH = kHeight - y - kStatusBarH;

    if (sliceMode)
    {
        // SLICE: pad grid on the left, state dot on the right.
        const int padGridW  = (int) (kWidth * 0.45f);
        const int dotFieldW = kWidth - padGridW;

        padGrid.setVisible (true);
        padGrid.setBounds       (0,        y, padGridW,  lowerH);
        stateDotField.setBounds (padGridW, y, dotFieldW, lowerH);

        // SAMPLE-mode controls hidden in SLICE.
        loopBtn.setVisible (false);
        reverseBtn.setVisible (false);
    }
    else
    {
        // SAMPLE: state dot on the left (45%), control panel on the right (55%).
        const int dotW    = (int) (kWidth * 0.45f);
        const int ctrlW   = kWidth - dotW;
        const int ctrlX   = dotW;

        padGrid.setVisible (false);
        stateDotField.setBounds (0, y, dotW, lowerH);

        // Three horizontal rows inside the control panel.
        // Row 1 (pitch + speed displays) ~55% of height
        // Row 2 (LOOP + REVERSE pills)    ~25%
        // Row 3 (EXPORT)                  ~20%
        const int rowPad = 10;
        const int rowTopY    = y + rowPad;
        const int rowTopH    = (int) (lowerH * 0.55f);
        const int rowMidY    = rowTopY + rowTopH;
        const int rowMidH    = (int) (lowerH * 0.22f);
        const int rowBotY    = rowMidY + rowMidH;
        const int rowBotH    = lowerH - (rowTopH + rowMidH) - rowPad;

        // Row 1: split pitch/speed 50/50.
        const int rowTopInnerX = ctrlX + 16;
        const int rowTopInnerW = ctrlW - 32;
        const int halfW = rowTopInnerW / 2;

        pitchDragRect = { rowTopInnerX,          rowTopY, halfW,  rowTopH };
        speedDragRect = { rowTopInnerX + halfW,  rowTopY, halfW,  rowTopH };

        // Row 2: loop + reverse pills, centred inside the panel.
        const int pillW = 86;
        const int pillH = 24;
        const int pillGap = 12;
        const int pillTotal = pillW * 2 + pillGap;
        const int pillY = rowMidY + (rowMidH - pillH) / 2;
        const int pillX = ctrlX + (ctrlW - pillTotal) / 2;

        loopBtn.setVisible (true);
        reverseBtn.setVisible (true);
        loopBtn.setBounds    (pillX,                         pillY, pillW, pillH);
        reverseBtn.setBounds (pillX + pillW + pillGap,       pillY, pillW, pillH);

       #if JucePlugin_Build_Standalone
        // Row 3: EXPORT right-aligned. In SAMPLE mode we override the
        // bounds already set by the capture-bar positioning above so the
        // button lives inside the control panel instead.
        const int exW = 80;
        const int exH = 24;
        const int exY = rowBotY + (rowBotH - exH) / 2;
        const int exX = ctrlX + ctrlW - exW - 16;
        exportBtn.setBounds (exX, exY, exW, exH);
       #else
        (void) rowBotY; (void) rowBotH;
       #endif
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Paint
// ══════════════════════════════════════════════════════════════════════════

void PresaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::BG());
    drawTopBar (g);
    drawCaptureBar (g);
    drawStatusBar (g);

    if (proc.getSamplerMode() == SamplerMode::Sample)
    {
        const int y = kTopBarH + kWaveformH + kCaptureBarH;
        const int lowerH = kHeight - y - kStatusBarH;
        const int dotW   = (int) (kWidth * 0.45f);
        drawSampleControlPanel (g, { dotW, y, kWidth - dotW, lowerH });
    }
}

void PresaEditor::drawSampleControlPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Panel background — slightly lighter than the main bg to separate
    // it from the state-dot field on the left.
    g.setColour (Palette::BG2());
    g.fillRect (bounds);

    // Pitch readout.
    {
        const int semis = proc.getPitchSemitones();
        juce::String t = (semis > 0 ? "+" : "") + juce::String (semis) + " st";

        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                               26.0f, juce::Font::plain));
        g.setColour (semis != 0 ? Palette::ACCENT() : Palette::TXT_HI());
        g.drawText (t, pitchDragRect.withTrimmedBottom (14),
                    juce::Justification::centred, false);

        g.setFont (mono (7.0f));
        g.setColour (Palette::TXT_DIM());
        g.drawText ("PITCH",
                    pitchDragRect.withTop (pitchDragRect.getBottom() - 12),
                    juce::Justification::centred, false);
    }

    // Speed readout.
    {
        const float sp = proc.getSpeedMultiplier();
        juce::String t = juce::String (sp, (sp == (int) sp) ? 1 : 2) + "x";

        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                               26.0f, juce::Font::plain));
        g.setColour (std::abs (sp - 1.0f) > 1e-3f
                       ? Palette::ACCENT()
                       : Palette::TXT_HI());
        g.drawText (t, speedDragRect.withTrimmedBottom (14),
                    juce::Justification::centred, false);

        g.setFont (mono (7.0f));
        g.setColour (Palette::TXT_DIM());
        g.drawText ("SPEED",
                    speedDragRect.withTop (speedDragRect.getBottom() - 12),
                    juce::Justification::centred, false);
    }
}

// ── Top bar ────────────────────────────────────────────────────────────────

void PresaEditor::drawTopBar (juce::Graphics& g)
{
    auto bar = juce::Rectangle<int> (0, 0, kWidth, kTopBarH);
    g.setColour (Palette::BG4());
    g.fillRect (bar);

    g.setFont (mono (10.0f));

    // "PRESA" left
    g.setColour (Palette::TXT_MID());
    g.drawText ("PRESA", 12, 0, 60, kTopBarH, juce::Justification::centredLeft, false);

    // Capture state dot
    g.setColour (getCaptureStateColour());
    g.fillEllipse (80.0f, (kTopBarH - 6.0f) / 2.0f, 6.0f, 6.0f);

    // Loopback device status / capture error / transient status (EXPORT etc).
    g.setFont (mono (8.0f));
    const auto transient = proc.getStatusMessage();
    if (transient.isNotEmpty())
    {
        // LINK_ACTIVE for success-y messages, ACCENT for the rest.
        const bool isSuccess = transient.startsWithIgnoreCase ("Exported");
        g.setColour (isSuccess ? Palette::LINK_ACTIVE() : Palette::ACCENT());
        g.drawText (transient, kWidth / 2 - 140, 0, 280, kTopBarH,
                    juce::Justification::centred, false);
    }
    else if (proc.captureErrorMessage.isNotEmpty())
    {
        g.setColour (Palette::ACCENT());
        g.drawText (proc.captureErrorMessage, kWidth / 2 - 120, 0, 240, kTopBarH,
                    juce::Justification::centred, false);
    }
    else
    {
        const bool statusOk = !proc.loopbackDeviceStatus.startsWith ("Error")
                           && !proc.loopbackDeviceStatus.contains ("required");
        g.setColour (statusOk ? Palette::TXT_DIM() : Palette::ACCENT());
        g.drawText (proc.loopbackDeviceStatus, kWidth / 2 - 80, 0, 160, kTopBarH,
                    juce::Justification::centred, false);
    }

    // Debug: input bus RMS (unconditional — shows if standalone mute is killing signal)
    float inRms = proc.captureBuffer.getInputRMS();
    g.setColour (inRms > 0.001f ? Palette::ACCENT() : Palette::TXT_DIM());
    g.drawText ("IN:" + juce::String (inRms, 3),
                kWidth - 260, 0, 70, kTopBarH,
                juce::Justification::centredRight, false);

    // Debug: capture RMS (from LoopbackCapture — bypasses mute)
    float capRms = proc.captureBuffer.getCaptureRMS();
    g.setColour (capRms > 0.001f ? Palette::ACCENT() : Palette::TXT_DIM());
    g.drawText ("CAP:" + juce::String (capRms, 3),
                kWidth - 180, 0, 76, kTopBarH,
                juce::Justification::centredRight, false);

    // CPU readout (placeholder)
    g.drawText ("CPU: --", kWidth - 80, 0, 68, kTopBarH,
                juce::Justification::centredRight, false);
}

// ── Capture bar ────────────────────────────────────────────────────────────

void PresaEditor::drawCaptureBar (juce::Graphics& g)
{
    int barY = kTopBarH + kWaveformH;
    auto bar = juce::Rectangle<int> (0, barY, kWidth, kCaptureBarH);
    g.setColour (Palette::BG3());
    g.fillRect (bar);

    // Root note display
    g.setFont (mono (9.0f));
    g.setColour (Palette::TXT_MID());
    g.drawText (noteName (rootNoteDisplay), rootNoteRect,
                juce::Justification::centred, false);

    // Arrows
    g.setColour (Palette::TXT_DIM());
    g.drawText ("<", rootNoteDnRect, juce::Justification::centred, false);
    g.drawText (">", rootNoteUpRect, juce::Justification::centred, false);

    // Capture state highlight on CAPTURE button
    if (proc.captureState.load() == static_cast<int> (CaptureState::Recording))
    {
        auto r = captureBtn.getBounds().toFloat();
        g.setColour (Palette::ACCENT().withAlpha (0.15f));
        g.fillRoundedRectangle (r.expanded (2.0f), 5.0f);
    }
}

// ── Status bar ─────────────────────────────────────────────────────────────

void PresaEditor::drawStatusBar (juce::Graphics& g)
{
    auto bar = juce::Rectangle<int> (0, kHeight - kStatusBarH, kWidth, kStatusBarH);
    g.setColour (Palette::BG4());
    g.fillRect (bar);

    g.setFont (mono (8.0f));
    g.setColour (Palette::TXT_DIM());

    // Sample rate / buffer size
    auto sr = proc.currentSampleRate.load();
    auto bs = proc.currentBlockSize.load();
    g.drawText (juce::String ((int) sr) + " / " + juce::String (bs),
                12, bar.getY(), 120, kStatusBarH,
                juce::Justification::centredLeft, false);

    // Loopback device (placeholder)
    g.drawText ("Loopback: --", kWidth / 2 - 60, bar.getY(), 120, kStatusBarH,
                juce::Justification::centred, false);

    // Format indicator
    #if JucePlugin_Build_VST3
        juce::String fmt = "VST3";
    #elif JucePlugin_Build_Standalone
        juce::String fmt = "STANDALONE";
    #else
        juce::String fmt = "PLUGIN";
    #endif
    g.drawText (fmt, kWidth - 100, bar.getY(), 88, kStatusBarH,
                juce::Justification::centredRight, false);
}

// ══════════════════════════════════════════════════════════════════════════
// Mouse — root note arrows
// ══════════════════════════════════════════════════════════════════════════

void PresaEditor::mouseDown (const juce::MouseEvent& e)
{
    if (rootNoteDnRect.contains (e.getPosition()))
    {
        rootNoteDisplay = juce::jmax (0, rootNoteDisplay - 1);
        proc.rootNote.store (rootNoteDisplay);
        repaint();
        return;
    }
    if (rootNoteUpRect.contains (e.getPosition()))
    {
        rootNoteDisplay = juce::jmin (127, rootNoteDisplay + 1);
        proc.rootNote.store (rootNoteDisplay);
        repaint();
        return;
    }

    if (proc.getSamplerMode() == SamplerMode::Sample)
    {
        if (pitchDragRect.contains (e.getPosition()))
        {
            sampleDrag       = SampleDragTarget::Pitch;
            sampleDragStartY = e.getPosition().getY();
            pitchAtDragStart = proc.getPitchSemitones();
            return;
        }
        if (speedDragRect.contains (e.getPosition()))
        {
            sampleDrag       = SampleDragTarget::Speed;
            sampleDragStartY = e.getPosition().getY();
            speedAtDragStart = proc.getSpeedMultiplier();
            return;
        }
    }
}

void PresaEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (sampleDrag == SampleDragTarget::None) return;

    const int dy = e.getPosition().getY() - sampleDragStartY;

    if (sampleDrag == SampleDragTarget::Pitch)
    {
        // 10 px per semitone; drag up raises pitch to match the waveform drag.
        const int delta = -dy / 10;
        const int target = juce::jlimit (-24, 24, pitchAtDragStart + delta);
        if (target != proc.getPitchSemitones())
        {
            proc.setPitchSemitones (target);
            repaint();
        }
    }
    else if (sampleDrag == SampleDragTarget::Speed)
    {
        // 18 px per discrete speed step — gives a sensible drag feel at the
        // ladder's granularity (8 steps over a ~140 px throw).
        const int startIdx = speedIndex (speedAtDragStart);
        const int step = -dy / 18;
        const int idx = juce::jlimit (0, kSpeedStepCount - 1, startIdx + step);
        const float target = speedFromIndex (idx);
        if (std::abs (target - proc.getSpeedMultiplier()) > 1e-4f)
        {
            proc.setSpeedMultiplier (target);
            repaint();
        }
    }
}

void PresaEditor::mouseUp (const juce::MouseEvent&)
{
    sampleDrag = SampleDragTarget::None;
}

#if JucePlugin_Build_Standalone
// ══════════════════════════════════════════════════════════════════════════
// Export — write the current capture (or trim window in SAMPLE mode) to a
// stereo 32-bit float WAV on the Desktop.
// ══════════════════════════════════════════════════════════════════════════

void PresaEditor::flashExportButton (bool success)
{
    exportFlashSuccess = success;
    exportFlashUntilMs = juce::Time::getMillisecondCounter() + 500;
    exportBtn.setColour (juce::TextButton::buttonColourId,
                         success ? Palette::LINK_ACTIVE()
                                 : juce::Colour (0xFFAA3A3A));
    exportBtn.setColour (juce::TextButton::textColourOffId, Palette::TXT_HI());
    exportBtn.repaint();
}

void PresaEditor::exportSample()
{
    auto& buf = proc.getSamplePlayer().getSampleBuffer();
    if (buf.getNumSamples() == 0)
    {
        flashExportButton (false);
        proc.setStatusMessage ("No sample to export");
        return;
    }

    const int startSample = proc.getExportStart();
    const int endSample   = proc.getExportEnd();
    const int numSamples  = endSample - startSample;

    if (numSamples <= 0)
    {
        flashExportButton (false);
        proc.setStatusMessage ("Export range is empty");
        return;
    }

    auto folder = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                    .getChildFile ("Presa Samples");
    if (! folder.exists())
    {
        auto result = folder.createDirectory();
        if (! result.wasOk())
        {
            flashExportButton (false);
            proc.setStatusMessage ("Export failed: cannot create folder");
            return;
        }
    }

    auto now = juce::Time::getCurrentTime();
    auto filename = juce::String ("Presa_")
                  + now.formatted ("%Y%m%d_%H%M%S")
                  + ".wav";
    auto outputFile = folder.getChildFile (filename);

    // Delete any stale file with the same name so createWriterFor doesn't
    // append onto an existing WAV header.
    if (outputFile.existsAsFile())
        outputFile.deleteFile();

    // WavAudioFormat takes ownership of the FileOutputStream on success;
    // we must release() only after createWriterFor returns a non-null writer.
    auto fileStream = std::make_unique<juce::FileOutputStream> (outputFile);
    if (! fileStream->openedOk())
    {
        flashExportButton (false);
        proc.setStatusMessage ("Export failed: cannot open file");
        return;
    }

    double sr = proc.getCaptureSampleRate();
    if (sr <= 0.0)
        sr = proc.currentSampleRate.load();   // fallback if capture is idle
    if (sr <= 0.0)
        sr = 48000.0;

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (fileStream.get(),
                                sr,
                                (unsigned int) buf.getNumChannels(),
                                32,
                                {},
                                0));

    if (writer == nullptr)
    {
        flashExportButton (false);
        proc.setStatusMessage ("Export failed: writer creation");
        return;
    }

    // Writer now owns the stream.
    fileStream.release();

    const bool ok = writer->writeFromAudioSampleBuffer (buf, startSample, numSamples);
    writer.reset();  // flush + close

    if (ok)
    {
        flashExportButton (true);
        proc.setStatusMessage ("Exported: " + filename);
    }
    else
    {
        outputFile.deleteFile();
        flashExportButton (false);
        proc.setStatusMessage ("Export failed: write error");
    }
}
#endif // JucePlugin_Build_Standalone
