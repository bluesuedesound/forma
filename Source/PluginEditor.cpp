#include "PluginEditor.h"
using namespace C;

const juce::StringArray FormaEditor::keyNames =
    { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };

juce::Font FormaEditor::mono (float h) const
{ return juce::Font (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain); }

juce::Font FormaEditor::sans (float h, bool bold) const
{ return juce::Font (juce::Font::getDefaultSansSerifFontName(), h,
                     bold ? juce::Font::bold : juce::Font::plain); }

// Helper: interpolate two colours by t
static juce::Colour lerpColour (juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t));
}

// ─── Lofi palette additions (kept local; the namespace-C palette stays
// load-bearing for legacy controls). ───
namespace LofiC {
    const juce::Colour BG_DEEP    (0xFF0C0805);
    const juce::Colour BG_BASE    (0xFF14100C);
    const juce::Colour AMBER      (0xFFD97A3C);
    const juce::Colour AMBER_SOFT (0xFFB85A26);
    const juce::Colour AMBER_HOT  (0xFFF0A060);
    const juce::Colour DEEP_RED   (0xFF5A1F10);
    const juce::Colour INK_HERO   (0xFFF0E0C8);
}

// ─── Grain texture — built once in the editor ctor, tiled at low opacity. ───

void FormaEditor::buildGrainTile()
{
    constexpr int N = 256;
    grainTile = juce::Image (juce::Image::ARGB, N, N, true);
    juce::Random rng (0xF09A4B12);  // deterministic seed
    juce::Image::BitmapData bd (grainTile, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < N; ++y)
    {
        for (int x = 0; x < N; ++x)
        {
            // Warm-tinted noise: weighted toward amber/brown, away from cool grey.
            float v = rng.nextFloat();
            // Bias the brightness curve so most pixels are dim with rare bright specks.
            v = v * v;
            // Channel weights that read brown/amber rather than neutral grey.
            juce::uint8 r = (juce::uint8) juce::jlimit (0, 255, (int) (v * 220.0f));
            juce::uint8 grn = (juce::uint8) juce::jlimit (0, 255, (int) (v * 150.0f));
            juce::uint8 b = (juce::uint8) juce::jlimit (0, 255, (int) (v * 90.0f));
            // Modulated alpha — most pixels are fully transparent or near-zero,
            // sprinkled bright grains punctuate. Real overlay opacity is controlled
            // by the caller; here we just set per-pixel weight.
            juce::uint8 a = (juce::uint8) juce::jlimit (0, 255, (int) (v * 255.0f));
            bd.setPixelColour (x, y, juce::Colour::fromRGBA (r, grn, b, a));
        }
    }
}

void FormaEditor::drawGrainOverlay (juce::Graphics& g, juce::Rectangle<int> area, float opacity)
{
    if (! grainTile.isValid()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (area);
    auto fa = (float) juce::jlimit (0.0f, 1.0f, opacity);
    g.setOpacity (fa);
    const int N = grainTile.getWidth();
    for (int y = area.getY(); y < area.getBottom(); y += N)
        for (int x = area.getX(); x < area.getRight(); x += N)
            g.drawImageAt (grainTile, x, y);
    g.setOpacity (1.0f);
}

bool FormaEditor::anyPillAnimating() const
{
    for (int i = 0; i < 7; ++i)
        if (pillAnim[i].state == PillAnim::State::Flashing)
            return true;
    return false;
}

// ══════════════════���════════════════════════════��═════════════════════════
// CONSTRUCTOR
// ══════════��════════════════��═════════════════════���═══════════════════════

FormaEditor::FormaEditor (FormaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (780, 564);
    currentSyncMode = proc.syncMode.load();
    currentBgColor = juce::Colour (kMoods[0].bgColor);
    targetBgColor  = currentBgColor;

    // Build grain tile once. 256x256, tile-able, warm-tinted.
    buildGrainTile();

    // Load XY dot from processor
    dotX = proc.xyDotX.load();
    dotY = proc.xyDotY.load();

    // Initialize thermal wave phases
    juce::Random rng;
    for (int i = 0; i < 4; ++i)
    {
        voidPhases[i] = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        ringPhases[i] = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    }
    updateWaveVelocities (0);

    updateChordLabels();

    // Rehydrate all UI mirror state from the processor. Without this the
    // bass pattern grid and arp step grid (and every other mirrored control)
    // would show their struct defaults every time the UI is closed and
    // reopened, even though the audio state is intact.
    syncUIFromProcessor();

    startTimerHz (60);
}

FormaEditor::~FormaEditor() { stopTimer(); }

// ════════════���═══════════════════════════════════════════════════��════════
// TIMER
// ═══════════════════════════════════════════════════════════════��═════════

void FormaEditor::timerCallback()
{
    float dt = 1.0f / 60.0f;

    // BPM from host playhead
    if (auto* ph = proc.getPlayHead())
    {
        auto posInfo = ph->getPosition();
        if (posInfo.hasValue())
        {
            if (auto bpm = posInfo->getBpm())
                currentBpm = *bpm;
        }
    }
    if (proc.linkBpm.load() > 0)
        currentBpm = proc.linkBpm.load();
    isLinkActive = proc.linkActive.load();

    // Suggestion pulse
    sugPulse += 0.044f * sugDir;
    if (sugPulse >= 1.0f) { sugDir = -1.0f; sugPulse = 1.0f; }
    if (sugPulse <= 0.0f) { sugDir =  1.0f; sugPulse = 0.0f; }

    // Sync XY dot from processor when not dragging
    if (!draggingDot)
    {
        if (currentSyncMode == 0)  // Full
        {
            dotX = proc.feelAmount.load();
            dotY = 1.0f - proc.colorAmount.load();
        }
        else if (currentSyncMode == 1)  // Expressive
        {
            dotY = 1.0f - proc.colorAmount.load();
        }
    }

    // Glow blob lerps toward dot
    glowX += (dotX - glowX) * 0.08f;
    glowY += (dotY - glowY) * 0.08f;

    // Active degree tracking
    int deg = proc.activeDegree.load();
    if (deg != lastDegree)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 7; ++i)
        {
            bool isActive = (i + 1 == deg);
            chords[i].pressed = isActive;

            // Pill state machine: when a pill becomes the active degree,
            // start the chord-name flash animation. When it leaves active,
            // either return to Resting (if no flash pending) or let the
            // flash finish (timer drives the transition below).
            if (isActive && pillAnim[i].state != PillAnim::State::Flashing)
            {
                pillAnim[i].state = PillAnim::State::Flashing;
                pillAnim[i].flashStartMs = now;
            }
            else if (! isActive && pillAnim[i].state == PillAnim::State::Active)
            {
                pillAnim[i].state = PillAnim::State::Resting;
            }
        }
        lastDegree = deg;

        for (int i = 0; i < 7; ++i) { chords[i].sug1 = false; chords[i].sug2 = false; }
        if (proc.suggestionsVisible.load())
        {
            int s1 = proc.primarySuggestion.load();
            int s2 = proc.secondarySuggestion.load();
            if (s1 >= 1 && s1 <= 7) chords[s1 - 1].sug1 = true;
            if (s2 >= 1 && s2 <= 7) chords[s2 - 1].sug2 = true;
        }

        statusChord = (deg >= 1 && deg <= 7) ? proc.currentChordName : juce::String ("Ready");
    }

    // Advance the flash state machine.
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 7; ++i)
        {
            if (pillAnim[i].state == PillAnim::State::Flashing
                && (now - pillAnim[i].flashStartMs) > 1100.0)
            {
                pillAnim[i].state = chords[i].pressed ? PillAnim::State::Active
                                                     : PillAnim::State::Resting;
            }
        }
    }

    // ── Thermal wave animation ──
    lastKnownDeg = deg;

    // Update wave phases
    int moodI = proc.harmonyEngine.getCurrentMoodIndex();
    if (moodI >= 0 && moodI < 8 && moodI != currentThermalIdx)
    {
        currentThermalIdx = moodI;
        updateWaveVelocities (moodI);
    }

    for (int i = 0; i < 4; ++i)
    {
        float dir = (i % 2 == 0) ? 1.0f : -1.0f;
        voidPhases[i] += voidPVels[i] * dt * dir;
        ringPhases[i] += ringPVels[i] * dt * dir;
        if (voidPhases[i] >  1000.0f) voidPhases[i] -= 1000.0f;
        if (voidPhases[i] < -1000.0f) voidPhases[i] += 1000.0f;
        if (ringPhases[i] >  1000.0f) ringPhases[i] -= 1000.0f;
        if (ringPhases[i] < -1000.0f) ringPhases[i] += 1000.0f;
    }

    breathePhase += dt * 0.7f;
    if (breathePhase > juce::MathConstants<float>::twoPi)
        breathePhase -= juce::MathConstants<float>::twoPi;

    // Progression name timeout
    if (proc.currentProgressionName.isNotEmpty())
        progNameAge = 0.0f;
    progNameAge += dt;
    if (progNameAge > 3.0f && proc.currentProgressionName.isNotEmpty())
        proc.currentProgressionName = juce::String();

    // Mood background color transition (400ms)
    if (bgTransProgress < 1.0f)
    {
        bgTransProgress += dt / 0.4f;
        if (bgTransProgress > 1.0f) bgTransProgress = 1.0f;
        float t = bgTransProgress * bgTransProgress * (3.0f - 2.0f * bgTransProgress);
        currentBgColor = currentBgColor.interpolatedWith (targetBgColor, t);
    }

    // Mood XY dot transition
    if (moodTransitioning && !draggingDot)
    {
        moodTransProgress += dt * 2.0f;
        if (moodTransProgress >= 1.0f)
        {
            moodTransProgress = 1.0f;
            moodTransitioning = false;
        }
        float t = moodTransProgress * moodTransProgress * (3.0f - 2.0f * moodTransProgress);
        dotX = dotX + (targetDotX - dotX) * t;
        dotY = dotY + (targetDotY - dotY) * t;
        proc.feelAmount.store (dotX);
        proc.colorAmount.store (1.0f - dotY);
        proc.harmonyEngine.setColorAmount (1.0f - dotY);
        proc.driftAmount.store (proc.getDriftForMoodAndFeel (dotX));
        proc.xyDotX.store (dotX);
        proc.xyDotY.store (dotY);
        updateChordLabels();
    }

    // Decay reset button flash
    if (resetFlashTimer > 0.0f)
        resetFlashTimer -= dt;

    // Sync suggestions toggle from processor
    suggestionsOn = proc.suggestionsVisible.load();

    repaint();
}

// ══════���══════════════════════════════════════════════════════════════════
// UPDATE HELPERS
// ══════════════════════════���═════════════════════════════��════════════════

void FormaEditor::updateChordLabels()
{
    static const char* up[] = {"I","II","III","IV","V","VI","VII"};
    static const char* lo[] = {"i","ii","iii","iv","v","vi","vii"};
    for (int i = 0; i < 7; ++i)
    {
        int d = i + 1;
        auto q = proc.harmonyEngine.getChordQuality (d);
        chords[i].name = proc.harmonyEngine.getChordShort (d);
        if      (q == "M") { chords[i].qual = Major;      chords[i].roman = up[i]; chords[i].qualLabel = "major"; }
        else if (q == "m") { chords[i].qual = Minor;      chords[i].roman = lo[i]; chords[i].qualLabel = "minor"; }
        else if (q == "d") { chords[i].qual = Diminished; chords[i].roman = juce::String(lo[i]) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")); chords[i].qualLabel = "dim"; }
        else if (q == "A") { chords[i].qual = Augmented;  chords[i].roman = juce::String(up[i]) + "+"; chords[i].qualLabel = "aug"; }
        else               { chords[i].qual = Major;      chords[i].roman = up[i]; chords[i].qualLabel = ""; }
    }
}

void FormaEditor::setMood (int idx)
{
    moodIdx = idx;
    proc.harmonyEngine.setMood (HarmonyEngine::moodNames[idx]);
    proc.applyMoodDefaults (idx);

    // Start XY dot transition
    targetDotX = proc.feelAmount.load();
    targetDotY = 1.0f - proc.colorAmount.load();
    moodTransitioning = true;
    moodTransProgress = 0.0f;

    // Start bg color transition
    targetBgColor = juce::Colour (kMoods[juce::jlimit (0, kNumMoods - 1, idx)].bgColor);
    bgTransProgress = 0.0f;

    updateChordLabels();
    repaint();
}

void FormaEditor::setKey (int idx)
{
    keyIdx = idx;
    proc.harmonyEngine.setKey (48 + idx);
    updateChordLabels();
    repaint();
}

void FormaEditor::syncUIFromProcessor()
{
    // Mood
    auto mood = proc.harmonyEngine.getCurrentMood();
    for (int i = 0; i < kNumMoods; ++i)
    {
        if (HarmonyEngine::moodNames[i] == mood)
        {
            moodIdx = i;
            currentBgColor = juce::Colour (kMoods[i].bgColor);
            targetBgColor  = currentBgColor;
            bgTransProgress = 1.0f;
            break;
        }
    }

    // Key
    keyIdx = juce::jlimit (0, 11, proc.harmonyEngine.getRootMidi() - 48);

    // XY dot
    dotX = proc.xyDotX.load();
    dotY = proc.xyDotY.load();
    glowX = dotX;
    glowY = dotY;

    // Voice toggles
    chordsEnabledUI = proc.chordsEnabled.load();
    bassEnabledUI   = proc.bassEnabled.load();

    // Bass controls
    bassOct         = proc.octaveBassParam.load();
    bassModeUI      = juce::jlimit (0, 2, proc.bassMode.load());
    bassTrigNoteUI  = juce::jlimit (0, 127, proc.bassTriggerNoteParam.load());
    bassVariationUI = proc.bassVariationAmount.load();

    // Sync
    currentSyncMode = proc.syncMode.load();

    // Sound + voicing
    currentSoundPreset = proc.currentSoundPreset.load();
    currentVoicingMode = proc.voicingMode.load();

    // Chord octave
    chordOctVal = proc.octaveChordParam.load();

    // Synth
    synthVol = proc.synthVolume.load();

    // Suggestions
    suggestionsOn = proc.suggestionsVisible.load();

    updateChordLabels();
    repaint();
}

// ════════════════════════════════════��════════════════════════════════════
// PAINT
// ═════════════════════════════════════════════════════════════════════════

void FormaEditor::paint (juce::Graphics& g)
{
    // Three-column layout: mood list (left), chord interface (center),
    // voice toggles + bass section (right).
    leftCol    = juce::Rectangle<int> (0,   40, 155, 500);
    centerCol  = juce::Rectangle<int> (155, 40, 465, 500);
    rightCol   = juce::Rectangle<int> (620, 40, 160, 500);

    // XY compass container centered in centerCol's top half.
    int circleAreaH = centerCol.getHeight() - 180;  // top zone = 320 px
    int compSize = 320;
    int compX = centerCol.getX() + (centerCol.getWidth() - compSize) / 2;
    int compY = centerCol.getY() + (circleAreaH - compSize) / 2;
    compassContainer = juce::Rectangle<int> (compX, compY, compSize, compSize);

    int padSize = 240;
    int padX = compassContainer.getCentreX() - padSize / 2;
    int padY = compassContainer.getCentreY() - padSize / 2;
    xyPadCircle = juce::Rectangle<int> (padX, padY, padSize, padSize);

    // Chord keys: 7 equal width across the bottom of centerCol.
    int ckY = centerCol.getY() + circleAreaH;
    int ckH = 154;
    int ckGap = 5;
    int ckTotalW = centerCol.getWidth();
    int ckW = (ckTotalW - ckGap * 6) / 7;
    for (int i = 0; i < 7; ++i)
        chordKeyRects[i] = juce::Rectangle<int> (centerCol.getX() + i * (ckW + ckGap), ckY + 14, ckW, ckH);

    g.fillAll (BG4);

    drawTopBar   (g);
    drawLeftCol  (g);
    drawCenter   (g);
    drawRightCol (g);
    drawStatusBar (g);

    if (advancedVisible)
        drawAdvanced (g);

    // Whole-UI grain overlay — last so it falls on every surface uniformly.
    drawGrainOverlay (g, getLocalBounds(), 0.04f);
}

// ════════════════════════════════��════════════════════════════════════════
// TOP BAR  (y=0, h=40, #0F0E0B)
// ════════════════════════════════════════════════════��════════════════════

void FormaEditor::drawTopBar (juce::Graphics& g)
{
    auto r = juce::Rectangle<int> (0, 0, getWidth(), 40);
    g.setColour (BG4);
    g.fillRect (r);
    g.setColour (BORDER);
    g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) getWidth());

    // Letterpress logo
    g.setFont (mono (15.0f));
    auto logo = r.withTrimmedLeft (18);
    g.setColour (juce::Colour (0xFF0A0908));
    g.drawText ("F O R M A", logo.translated (1, 1), juce::Justification::centredLeft);
    g.setColour (TXT_HI);
    g.drawText ("F O R M A", logo, juce::Justification::centredLeft);

    // Right side — right to left
    int rx = getWidth() - 14;

    // Gear button
    g.setFont (mono (10.0f));
    int gearW = 82;
    gearBtnRect = juce::Rectangle<int> (rx - gearW, r.getCentreY() - 10, gearW, 20);
    g.setColour (BORDER);
    g.drawRoundedRectangle (gearBtnRect.toFloat(), 4.0f, 1.0f);
    g.setColour (TXT_DIM);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\x99")) + " ADVANCED",
                gearBtnRect, juce::Justification::centred);
    rx -= gearW + 14;

    // BPM
    g.setFont (mono (10.0f));
    g.setColour (TXT_MID);
    juce::String bpmVal = juce::String (currentBpm, 1);
    g.drawText (bpmVal, juce::Rectangle<int> (rx - 50, r.getY(), 50, r.getHeight()),
                juce::Justification::centredRight);
    rx -= 52;
    g.setColour (TXT_DIM);
    g.drawText ("BPM", juce::Rectangle<int> (rx - 28, r.getY(), 28, r.getHeight()),
                juce::Justification::centredRight);
    rx -= 38;

    // Link indicator
    float linkCx = (float)(rx - 6);
    float linkCy = (float) r.getCentreY();
    linkRect = juce::Rectangle<int> ((int) linkCx - 24, r.getY(), 48, r.getHeight());

    if (isLinkActive)
    {
        g.setColour (juce::Colour (0xFF5AAA5A));
        g.fillEllipse (linkCx - 3, linkCy - 3, 6, 6);
        // Glow
        g.setColour (juce::Colour (0x405AAA5A));
        g.fillEllipse (linkCx - 6, linkCy - 6, 12, 12);
    }
    else
    {
        g.setColour (TXT_GHOST);
        g.fillEllipse (linkCx - 3, linkCy - 3, 6, 6);
    }
    g.setFont (mono (9.0f));
    g.setColour (TXT_DIM);
    g.drawText ("LINK", juce::Rectangle<int> ((int) linkCx + 6, r.getY(), 30, r.getHeight()),
                juce::Justification::centredLeft);
}

// ═══════���══════════════════════��══════════════════════════��═══════════════
// LEFT COLUMN  (x=0, y=40, w=155, h=500, #141210)
// ═════════════════════════════════════════════════════════════════════════

void FormaEditor::drawLeftCol (juce::Graphics& g)
{
    g.setColour (BG2);
    g.fillRect (leftCol);
    g.setColour (BORDER);
    g.drawVerticalLine (leftCol.getRight() - 1, (float) leftCol.getY(), (float) leftCol.getBottom());

    int px = leftCol.getX() + 15;
    int py = leftCol.getY() + 20;

    // MOOD header
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("MOOD", juce::Rectangle<int> (px, py, 80, 12), juce::Justification::centredLeft);
    py += 18;

    // 12 mood items (8 core + 4 pack)
    for (int i = 0; i < kNumMoods; ++i)
    {
        // Separator before pack moods
        if (i == 8)
        {
            g.setColour (BORDER);
            g.drawHorizontalLine (py + 2, (float) px, (float)(px + 123));
            g.setFont (mono (7.0f));
            g.setColour (TXT_DIM);
            g.drawText ("BRIGHT LIGHTS", juce::Rectangle<int> (px, py + 5, 123, 10),
                        juce::Justification::centred);
            py += 18;
        }

        auto pr = juce::Rectangle<int> (px, py, 123, 23);
        bool active = (i == moodIdx);
        bool hover  = (i == hoveredMoodIdx && !active);

        if (active)
        {
            g.setColour (juce::Colour (0xFF1E1C18));
            g.fillRoundedRectangle (pr.toFloat(), 4.0f);
            g.setColour (juce::Colour (0xFF5A3A20));
            g.drawRoundedRectangle (pr.toFloat(), 4.0f, 1.0f);
        }

        // Text
        g.setFont (mono (11.0f));
        if (active)
            g.setColour (ACCENT);
        else if (hover)
            g.setColour (juce::Colour (0xFF5A5750));
        else
            g.setColour (TXT_GHOST);

        g.drawText (juce::String (juce::CharPointer_UTF8 (kMoods[i].name)),
                    pr.withTrimmedLeft (10), juce::Justification::centredLeft);

        // Dot (right side, 5px)
        float dotCx = (float)(pr.getRight() - 12);
        float dotCy = (float) pr.getCentreY();
        if (active)
        {
            g.setColour (ACCENT);
            g.fillEllipse (dotCx - 2.5f, dotCy - 2.5f, 5, 5);
            // Glow
            g.setColour (juce::Colour (0xCCC8875A));
            g.fillEllipse (dotCx - 5, dotCy - 5, 10, 10);
        }
        else
        {
            g.setColour (BORDER);
            g.fillEllipse (dotCx - 2.5f, dotCy - 2.5f, 5, 5);
        }

        py += 25;
    }

    // ── Key + description pinned to bottom ──
    int bottomY = leftCol.getBottom() - 120;
    g.setColour (BORDER);
    g.drawHorizontalLine (bottomY, (float)(leftCol.getX() + 10), (float)(leftCol.getRight() - 10));
    bottomY += 16;

    // KEY label
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("KEY", juce::Rectangle<int> (px, bottomY, 50, 12), juce::Justification::centredLeft);
    bottomY += 16;

    // Key selector row
    auto downR = juce::Rectangle<int> (px, bottomY, 20, 20);
    g.setColour (BG2);
    g.fillRoundedRectangle (downR.toFloat(), 3.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (downR.toFloat(), 3.0f, 1.0f);
    g.setFont (mono (8.0f));
    g.setColour (TXT_DIM);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")), downR, juce::Justification::centred);

    // Key name (centered)
    g.setFont (mono (26.0f));
    g.setColour (TXT_HI);
    g.drawText (keyNames[keyIdx], juce::Rectangle<int> (px + 24, bottomY - 4, 70, 28), juce::Justification::centred);

    auto upR = juce::Rectangle<int> (px + 98, bottomY, 20, 20);
    g.setColour (BG2);
    g.fillRoundedRectangle (upR.toFloat(), 3.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (upR.toFloat(), 3.0f, 1.0f);
    g.setFont (mono (8.0f));
    g.setColour (TXT_DIM);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")), upR, juce::Justification::centred);

    bottomY += 26;

    // OCT stepper (chord octave — moved from advanced panel)
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("OCT", juce::Rectangle<int> (px, bottomY, 30, 12), juce::Justification::centredLeft);

    int octVal = proc.octaveChordParam.load();
    auto octMinR = juce::Rectangle<int> (px + 32, bottomY, 18, 18);
    g.setColour (BG2);
    g.fillRoundedRectangle (octMinR.toFloat(), 3.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (octMinR.toFloat(), 3.0f, 1.0f);
    g.setFont (mono (10.0f));
    g.setColour (TXT_DIM);
    g.drawText ("-", octMinR, juce::Justification::centred);

    juce::String octStr = octVal == 0 ? "0" : (octVal > 0 ? "+" + juce::String (octVal) : juce::String (octVal));
    g.setFont (mono (11.0f));
    g.setColour (octVal == 0 ? TXT_DIM : ACCENT);
    g.drawText (octStr, juce::Rectangle<int> (px + 54, bottomY, 28, 18), juce::Justification::centred);

    auto octPlR = juce::Rectangle<int> (px + 86, bottomY, 18, 18);
    g.setColour (BG2);
    g.fillRoundedRectangle (octPlR.toFloat(), 3.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (octPlR.toFloat(), 3.0f, 1.0f);
    g.setFont (mono (10.0f));
    g.setColour (TXT_DIM);
    g.drawText ("+", octPlR, juce::Justification::centred);

    // Store rects for click handling
    advChordOctMinRect = octMinR;
    advChordOctPlRect  = octPlR;

    bottomY += 22;

    // Mood description
    g.setFont (mono (9.0f));
    g.setColour (ACCENT);
    g.drawText (juce::String (juce::CharPointer_UTF8 (kMoods[moodIdx].desc)),
                juce::Rectangle<int> (px, bottomY, 123, 14), juce::Justification::centredLeft);
}

// ═════════════════════════���═══════════════════════════════════════════════
// CENTER COLUMN
// ═════════════════════════════════════════════════════════════���═══════════

void FormaEditor::drawCenter (juce::Graphics& g)
{
    // Mood-tinted background with transition
    g.setColour (currentBgColor);
    g.fillRect (centerCol);

    drawXYPad (g);

    int circleAreaH = centerCol.getHeight() - 180;
    int divY = centerCol.getY() + circleAreaH;
    g.setColour (BORDER);
    g.drawHorizontalLine (divY, (float) centerCol.getX(),
                          (float) centerCol.getRight());

    for (int i = 0; i < 7; ++i)
        drawChordKey (g, chordKeyRects[i], i);
}

// ═════════════════════════════════════════════════════════════════════════
// REACTIVE RING
// ═════��═══════════════════════════��═══════════════════════════════════════



// Mood thermal palette
struct MoodThermalDef {
    juce::uint32 voidCol, inner, mid1, mid2, outer, edge;
    float accentHue;
    float wFreq[4], wAmp[4], wSpd[4];
    int wCount;
    juce::uint32 dotStroke;
};
static const MoodThermalDef kMoodThermals[kNumMoods] = {
    // Bright: warm amber, medium
    { 0xFF080602,0xFF140E04,0xFF3A200A,0xFF704010,0xFFA06020,0xFFC88838,38, {3,7,13,0},{7.0f,3.5f,1.7f,0},{2.0f,3.5f,5.5f,0},3, 0xFFA07828 },
    // Warm: golden, groove
    { 0xFF090802,0xFF130C02,0xFF342008,0xFF624010,0xFF906018,0xFFB88028,34, {2,5,9,0},{8.4f,4.2f,2.8f,0},{1.6f,2.75f,2.1f,0},3, 0xFFA07018 },
    // Dream: indigo, slow wide
    { 0xFF060508,0xFF0C0A14,0xFF1A1030,0xFF32205C,0xFF503880,0xFF7858A8,255,{1.5f,3.5f,6,11},{12.6f,7.0f,4.2f,2.1f},{0.88f,1.38f,0.70f,1.75f},4, 0xFF685898 },
    // Deep: burnt amber, heavy
    { 0xFF090702,0xFF120A03,0xFF321806,0xFF60300A,0xFF8A4C14,0xFFB06C22,24, {1.5f,4,8,0},{11.2f,5.6f,2.8f,0},{0.75f,1.50f,1.25f,0},3, 0xFF986018 },
    // Hollow: cold blue-gray, sparse
    { 0xFF050708,0xFF090C12,0xFF121820,0xFF1E2C3C,0xFF344858,0xFF506878,205,{1,4,9,0},{4.2f,8.4f,2.1f,0},{0.45f,0.33f,0.70f,0},3, 0xFF405868 },
    // Tender: plum, flutter
    { 0xFF080507,0xFF120810,0xFF2C1028,0xFF501E44,0xFF783060,0xFF9C5078,318,{2,4.5f,8,0},{7.0f,4.2f,2.1f,0},{1.88f,3.25f,1.50f,0},3, 0xFF906070 },
    // Tense: dark red, rapid
    { 0xFF0A0302,0xFF180604,0xFF3C1008,0xFF681A0C,0xFF902814,0xFFB04020,10, {5,9,17,23},{5.6f,4.2f,2.8f,1.4f},{5.5f,8.75f,12.0f,9.75f},4, 0xFFA03018 },
    // Dusk: warm gold, layered
    { 0xFF090802,0xFF140C02,0xFF382008,0xFF6A440E,0xFF9C6C18,0xFFC49028,38, {1.8f,4,7,12},{9.8f,5.6f,2.8f,2.1f},{1.13f,1.88f,1.38f,2.50f},4, 0xFFB08020 },
    // ── Bright Lights pack ──
    // Crest: sky blue to warm white — clean, airy, bouncy
    { 0xFF040810,0xFF0A1420,0xFF1A3050,0xFF3A6088,0xFF5A90B8,0xFF87CEEB,200,{4,8,14,0},{4.0f,2.0f,1.0f,0},{1.2f,2.0f,3.2f,0},3, 0xFF6AACCC },
    // Nocturne: deep charcoal to cool blue-violet — slow, breathing
    { 0xFF060610,0xFF0C0C18,0xFF1A1A30,0xFF2A2A4A,0xFF3A3A5A,0xFF4A4E6B,240,{1.5f,4,9,0},{7.0f,4.0f,2.0f,0},{0.4f,0.7f,0.5f,0},3, 0xFF3A4060 },
    // Shimmer: cool silver to electric blue — medium, mechanical
    { 0xFF060810,0xFF0C1218,0xFF1A2430,0xFF344858,0xFF507080,0xFFB0C4DE,210,{2,6,10,0},{3.0f,3.0f,2.0f,0},{0.9f,1.5f,2.1f,0},3, 0xFF7898B0 },
    // Static: hot pink to electric white — very fast, glitchy
    { 0xFF100408,0xFF200810,0xFF401020,0xFF802040,0xFFB04060,0xFFFF69B4,330,{6,11,18,25},{3.0f,3.0f,2.0f,1.5f},{3.0f,4.5f,6.0f,5.2f},4, 0xFFD04888 },
};

void FormaEditor::updateWaveVelocities (int moodIdx)
{
    static const float kPhaseMults[4] = { 0.7f, 1.13f, 1.87f, 2.51f };
    auto& mt = kMoodThermals[juce::jlimit (0, kNumMoods - 1, moodIdx)];
    for (int i = 0; i < mt.wCount; ++i)
    {
        voidPVels[i] = mt.wSpd[i] * 0.55f * kPhaseMults[i];
        ringPVels[i] = mt.wSpd[i] * 0.40f * kPhaseMults[i];
    }
    for (int i = mt.wCount; i < 4; ++i) { voidPVels[i] = 0; ringPVels[i] = 0; }
}

float FormaEditor::voidRadius (float angle, float dotNX, float dotNY, float circleR)
{
    auto& mt = kMoodThermals[juce::jlimit (0, kNumMoods - 1, currentThermalIdx)];
    float colorAmt = (dotNY + 1.0f) * 0.5f;
    float feelAmt  = (dotNX + 1.0f) * 0.5f;
    float baseVoid = (0.46f - colorAmt * 0.18f) * circleR;

    float maxA = 0.001f;
    for (int i = 0; i < mt.wCount; ++i) maxA = juce::jmax (maxA, mt.wAmp[i]);

    float deform = 0.0f;
    for (int i = 0; i < mt.wCount; ++i)
        deform += (mt.wAmp[i] / maxA) * 5.5f * std::sin (angle * mt.wFreq[i] + voidPhases[i]);

    // Slow non-repeating drift — irrational time multipliers
    float t = breathePhase * 10.0f;  // slow time base
    float slowDrift = 8.0f * std::sin (t * 0.23f + angle * 2.0f)
                    + 6.0f * std::sin (t * 0.17f - angle * 3.0f)
                    + 4.0f * std::sin (t * 0.31f + angle * 1.0f);
    slowDrift *= (circleR * 0.018f);

    float feelStr = 1.0f + feelAmt * 0.28f * std::cos (angle + 0.4f);
    return (baseVoid + deform + slowDrift) * feelStr;
}

void FormaEditor::drawThermalCircle (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    auto& mt = kMoodThermals[juce::jlimit (0, kNumMoods - 1, currentThermalIdx)];
    float cx = centre.x, cy = centre.y;

    float dotNX = dotX * 2.0f - 1.0f;
    float dotNY = -(dotY * 2.0f - 1.0f);
    float dpx = cx + dotNX * radius * 0.68f;
    float dpy = cy - dotNY * radius * 0.68f;

    // 1. Deep base — almost-black warm well.
    g.setColour (LofiC::BG_DEEP);
    g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // 2. Posterized thermal — 3 discrete radial layers centered on the dot.
    //    Each is a coloured radial gradient that fades to fully-transparent;
    //    layered they read as soft posterized warmth without a per-frame blur.
    auto fillRadial = [&] (juce::Colour col, float extentRatio, float alpha)
    {
        float ext = juce::jmax (radius * extentRatio, 1.0f);
        juce::ColourGradient grad (col.withAlpha (alpha),     { dpx, dpy },
                                   col.withAlpha (0.0f),       { dpx + ext, dpy }, true);
        // Soft mid-stops to fake the blur without an offscreen image.
        grad.addColour (0.40, col.withAlpha (alpha * 0.85f));
        grad.addColour (0.70, col.withAlpha (alpha * 0.30f));
        g.setGradientFill (grad);
        g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    };

    // Layer 1: deep red base, wide.
    fillRadial (LofiC::DEEP_RED,   2.4f, 1.00f);
    // Mood-tinted mid layer for variety across moods (optional, low alpha).
    fillRadial (juce::Colour (mt.mid2), 1.6f, 0.45f);
    // Layer 2: amber core.
    fillRadial (LofiC::AMBER,      0.85f, 0.70f);
    // Layer 3: hot center.
    fillRadial (LofiC::AMBER_HOT,  0.32f, 0.80f);

    // 3. Void shape
    constexpr int VP = 128;
    juce::Path voidPath;
    for (int i = 0; i <= VP; ++i)
    {
        int idx = i % VP;
        float angle = (float) idx / VP * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        float vr = voidRadius (angle, dotNX, dotNY, radius);
        float px = dpx + vr * std::cos (angle);
        float py = dpy + vr * std::sin (angle);
        if (i == 0) voidPath.startNewSubPath (px, py);
        else        voidPath.lineTo (px, py);
    }
    voidPath.closeSubPath();

    g.setColour (juce::Colour (mt.voidCol));
    g.fillPath (voidPath);

    // Feathered void edge
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (voidPath);
        float avgR = voidRadius (0.0f, dotNX, dotNY, radius);
        juce::ColourGradient fade (juce::Colour (mt.voidCol).withAlpha (1.0f), { dpx, dpy },
                                    juce::Colour (mt.voidCol).withAlpha (0.0f), { dpx + avgR * 1.08f, dpy }, true);
        fade.addColour (0.62, juce::Colour (mt.voidCol).withAlpha (1.0f));
        fade.addColour (0.84, juce::Colour (mt.voidCol).withAlpha (0.8f));
        g.setGradientFill (fade);
        g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    }

    // 4. Outer ring
    constexpr int RP = 128;
    juce::Path ringPath;
    float maxA = 0.001f;
    for (int i = 0; i < mt.wCount; ++i) maxA = juce::jmax (maxA, mt.wAmp[i]);
    float maxDef = radius * 0.038f;

    for (int i = 0; i <= RP; ++i)
    {
        int idx = i % RP;
        float angle = (float) idx / RP * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        float def = 0.0f;
        for (int w = 0; w < mt.wCount; ++w)
            def += (mt.wAmp[w] / maxA) * std::sin (angle * mt.wFreq[w] + ringPhases[w]);
        float rr = radius - 2.0f + def * maxDef;
        float px = cx + rr * std::cos (angle);
        float py = cy + rr * std::sin (angle);
        if (i == 0) ringPath.startNewSubPath (px, py);
        else        ringPath.lineTo (px, py);
    }
    ringPath.closeSubPath();

    auto accentCol = juce::Colour::fromHSV (mt.accentHue / 360.0f, 0.45f, 0.68f, 1.0f);
    g.setColour (accentCol.withAlpha (0.10f));
    g.strokePath (ringPath, juce::PathStrokeType (7.0f));
    g.setColour (accentCol.withAlpha (0.18f));
    g.strokePath (ringPath, juce::PathStrokeType (3.0f));
    g.setColour (accentCol.withAlpha (0.38f));
    g.strokePath (ringPath, juce::PathStrokeType (1.0f));
}

void FormaEditor::drawThermalDot (juce::Graphics& g, float dpx, float dpy)
{
    // Amber halo: multiple draws at decreasing alpha and increasing radius
    // for a soft glow falloff.
    const auto amber = LofiC::AMBER;
    struct Halo { float r; float a; };
    static const Halo halos[] = {
        { 18.0f, 0.06f },
        { 13.0f, 0.10f },
        {  9.0f, 0.18f },
        {  7.0f, 0.30f },
    };
    for (const auto& h : halos)
    {
        g.setColour (amber.withAlpha (h.a));
        g.fillEllipse (dpx - h.r, dpy - h.r, h.r * 2.0f, h.r * 2.0f);
    }

    // Body: 14px circle, deep-bg fill, 2px amber stroke.
    constexpr float dotRad = 7.0f;
    g.setColour (LofiC::BG_DEEP);
    g.fillEllipse (dpx - dotRad, dpy - dotRad, dotRad * 2.0f, dotRad * 2.0f);
    g.setColour (amber);
    juce::Path dp;
    dp.addEllipse (dpx - dotRad, dpy - dotRad, dotRad * 2.0f, dotRad * 2.0f);
    g.strokePath (dp, juce::PathStrokeType (2.0f));

    // Tiny hot specular speck.
    g.setColour (LofiC::AMBER_HOT.withAlpha (0.85f));
    g.fillEllipse (dpx - 1.4f, dpy - 1.4f, 2.8f, 2.8f);
}



void FormaEditor::drawXYPad (juce::Graphics& g)
{
    auto r = xyPadCircle.toFloat();
    float cx = r.getCentreX(), cy = r.getCentreY(), rad = r.getWidth() * 0.5f;

    float colorAmt = proc.colorAmount.load();
    float feelAmt  = proc.feelAmount.load();

    // ── Compass labels — positioned just outside circle edge ──
    g.setFont (mono (12.0f));
    auto dim   = juce::Colour (0xFF2E2A24);
    auto bright = ACCENT;

    // Helper: draw label with 3-pass glow when active
    auto drawLabel = [&] (const juce::String& text, int lx, int ly, int lw, int lh,
                          juce::Justification just, juce::Colour col, float activity)
    {
        if (activity > 0.3f)
        {
            float a = (activity - 0.3f) * 0.5f;
            g.setColour (col.withAlpha (a * 0.15f));
            g.drawText (text, juce::Rectangle<int> (lx + 1, ly + 1, lw, lh), just);
            g.setColour (col.withAlpha (a * 0.10f));
            g.drawText (text, juce::Rectangle<int> (lx - 1, ly - 1, lw, lh), just);
        }
        g.setColour (col);
        g.drawText (text, juce::Rectangle<int> (lx, ly, lw, lh), just);
    };

    // RICH — 14px above circle top
    auto richCol = lerpColour (dim, bright, colorAmt);
    drawLabel ("RICH", (int)(cx - 24), (int)(cy - rad - 20), 48, 14,
               juce::Justification::centred, richCol, colorAmt);

    // SPARSE — 6px below circle bottom
    auto sparseCol = lerpColour (bright, dim, colorAmt);
    drawLabel ("SPARSE", (int)(cx - 32), (int)(cy + rad + 6), 64, 14,
               juce::Justification::centred, sparseCol, 1.0f - colorAmt);

    // TIGHT — 6px left of circle
    auto tightCol = lerpColour (bright, dim, feelAmt);
    drawLabel ("TIGHT", (int)(cx - rad - 46), (int)(cy - 7), 44, 14,
               juce::Justification::centredRight, tightCol, 1.0f - feelAmt);

    // LOOSE — 6px right of circle
    auto looseCol = lerpColour (dim, bright, feelAmt);
    drawLabel ("LOOSE", (int)(cx + rad + 2), (int)(cy - 7), 44, 14,
               juce::Justification::centredLeft, looseCol, feelAmt);

    // Clip to circle and draw organic bloom
    {
        juce::Path clipPath;
        clipPath.addEllipse (r);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clipPath);
        drawThermalCircle (g, { cx, cy }, rad);

        // Lofi grain inside the pad — gives the thermal an analog texture.
        drawGrainOverlay (g, r.toNearestInt(), 0.18f);

        // Vignette: dark ring at the outer edge pushing focus toward center.
        juce::ColourGradient vignette (juce::Colour::fromFloatRGBA (0, 0, 0, 0.0f),
                                       { cx, cy },
                                       juce::Colour::fromFloatRGBA (0, 0, 0, 0.55f),
                                       { cx + rad, cy }, true);
        vignette.addColour (0.55, juce::Colour::fromFloatRGBA (0, 0, 0, 0.0f));
        vignette.addColour (0.80, juce::Colour::fromFloatRGBA (0, 0, 0, 0.18f));
        g.setGradientFill (vignette);
        g.fillEllipse (r);
    }

    // Dot outside clip scope — sits on the ring with an amber halo.
    {
        float dnx = dotX * 2.0f - 1.0f;
        float dny = -((dotY * 2.0f) - 1.0f);
        float dpx = cx + dnx * rad * 0.68f;
        float dpy = cy - dny * rad * 0.68f;
        drawThermalDot (g, dpx, dpy);
    }

    // Border
    g.setColour (juce::Colour (0xFF2A2820));
    g.drawEllipse (r, 1.0f);
}

// ═══════════════════════════��═══════════════════════════��═════════════════
// CHORD KEY
// ═══��════════════════════════════════���════════════════════════════════════

void FormaEditor::drawChordKey (juce::Graphics& g, juce::Rectangle<int> r, int idx)
{
    auto& ch = chords[idx];
    auto& anim = pillAnim[idx];
    float rad = 10.0f;
    auto rf = r.toFloat();
    bool isHovered = (idx == hoveredChordIdx && !ch.pressed);
    bool isActive  = ch.pressed || anim.state == PillAnim::State::Active;
    bool isFlashing = (anim.state == PillAnim::State::Flashing);

    // ── Suggestion dot (pulsing amber for primary, cool blue for secondary).
    if (ch.sug1)
    {
        float sx = (float) r.getRight() - 8.0f;
        float sy = (float)(r.getY() + 8.0f);
        float alpha = 0.55f + sugPulse * 0.45f;
        g.setColour (LofiC::AMBER.withAlpha (alpha * 0.35f));
        g.fillEllipse (sx - 7, sy - 7, 14, 14);
        g.setColour (LofiC::AMBER.withAlpha (alpha));
        g.fillEllipse (sx - 3, sy - 3, 6, 6);
    }
    else if (ch.sug2)
    {
        float sx = (float) r.getRight() - 8.0f;
        float sy = (float)(r.getY() + 8.0f);
        g.setColour (SUGGEST2.withAlpha (0.75f));
        g.fillEllipse (sx - 3, sy - 3, 6, 6);
        g.setColour (SUGGEST2.withAlpha (0.28f));
        g.fillEllipse (sx - 6, sy - 6, 12, 12);
    }

    // ── 1. Background: linear gradient bg-elev → bg-card with inset highlight.
    {
        juce::ColourGradient bg (juce::Colour (0xFF20180F), rf.getX(), rf.getY(),
                                 juce::Colour (0xFF1A1410), rf.getX(), rf.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (rf, rad);

        // Subtle inset highlight along the top edge.
        juce::ColourGradient topGloss (juce::Colour::fromFloatRGBA (1.0f, 0.92f, 0.78f, 0.05f),
                                       rf.getX(), rf.getY(),
                                       juce::Colour::fromFloatRGBA (0, 0, 0, 0.0f),
                                       rf.getX(), rf.getY() + 12.0f, false);
        g.setGradientFill (topGloss);
        g.fillRoundedRectangle (rf.withHeight (12.0f), rad);
    }

    // Lofi grain inside the pill, masked to its rounded rect.
    {
        juce::Path clip;
        clip.addRoundedRectangle (rf, rad);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);
        drawGrainOverlay (g, r, 0.12f);
    }

    // ── 2. Active glow: amber radial from the pill's bottom upward.
    if (isActive)
    {
        juce::ColourGradient glow (LofiC::AMBER.withAlpha (0.25f),
                                   rf.getCentreX(), rf.getBottom(),
                                   LofiC::AMBER.withAlpha (0.0f),
                                   rf.getCentreX(), rf.getBottom() - rf.getHeight() * 0.7f,
                                   false);
        g.setGradientFill (glow);
        juce::Path clip;
        clip.addRoundedRectangle (rf, rad);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);
        g.fillAll();
    }

    // Hover whisper (subtle).
    if (isHovered)
    {
        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.92f, 0.78f, 0.025f));
        g.fillRoundedRectangle (rf, rad);
    }

    // ── 3. Quality stripe (left edge, 2px).
    if (ch.qual == Minor)
    { g.setColour (juce::Colour (0xFF2A2820)); g.fillRect (rf.getX() + 1, rf.getY() + 12, 2.0f, rf.getHeight() - 24); }
    else if (ch.qual == Diminished)
    { g.setColour (ACCENT_DK); g.fillRect (rf.getX() + 1, rf.getY() + 12, 2.0f, rf.getHeight() - 24); }
    else if (ch.qual == Augmented)
    { g.setColour (juce::Colour (0xFF2A4A5A)); g.fillRect (rf.getX() + 1, rf.getY() + 12, 2.0f, rf.getHeight() - 24); }

    // ── 4. Border + outer glow on active.
    if (isActive)
    {
        g.setColour (LofiC::AMBER.withAlpha (0.20f));
        g.drawRoundedRectangle (rf.expanded (2.0f), rad + 2.0f, 2.0f);
        g.setColour (LofiC::AMBER);
        g.drawRoundedRectangle (rf, rad, 1.2f);
    }
    else
    {
        g.setColour (isHovered ? juce::Colour (0xFF3A3528) : juce::Colour (0xFF2A1F15));
        g.drawRoundedRectangle (rf, rad, 1.0f);
    }

    // ── 5. Text. Resting/Active = degree (roman) + chord name + quality.
    //    Flashing = chord-name reveal animation: degree fades+slides up,
    //    chord name slides+fades in, holds, then reverses for the last
    //    third of the window.
    int textBot = r.getBottom() - 18;
    int romanY = textBot - 36;
    int nameY  = textBot - 22;
    int qualY  = textBot - 2;

    auto easeOutCubic = [] (float t) {
        t = juce::jlimit (0.0f, 1.0f, t);
        float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    };

    // Default text colors.
    juce::Colour romanCol = isActive ? juce::Colour (0xFFA89880) : juce::Colour (0xFF6A5A48);
    juce::Colour nameCol  = isActive ? LofiC::INK_HERO         : juce::Colour (0xFF8C7A65);
    juce::Colour qualCol  = isActive ? juce::Colour (0xFF8C7A65) : juce::Colour (0xFF4A4035);

    if (isFlashing)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float total = 1100.0f;
        float p = juce::jlimit (0.0f, 1.0f, (float)((now - anim.flashStartMs) / total));

        // Three sub-phases: 0..0.32 in, 0.32..0.68 hold, 0.68..1 out.
        float tIn  = juce::jlimit (0.0f, 1.0f, p / 0.32f);
        float tOut = juce::jlimit (0.0f, 1.0f, (p - 0.68f) / 0.32f);

        float reveal;
        if (p < 0.32f)      reveal = easeOutCubic (tIn);
        else if (p > 0.68f) reveal = 1.0f - easeOutCubic (tOut);
        else                reveal = 1.0f;

        // Degree opacity / translate: 1 → 0 → 1, sliding up by 4 px in middle.
        float degOpacity = 1.0f - reveal;
        float degDy      = -4.0f * reveal;
        // Chord-name reveal: opacity 0 → 1 → 0, sliding from below to above.
        float nameOpacity = reveal;
        float nameDy      = 4.0f * (1.0f - reveal);  // starts at +4, ends at 0 mid; then back to -4.
        if (p > 0.5f) nameDy = -4.0f * (reveal - 0.5f) * 2.0f;

        // Roman degree (faded out + drifting up).
        g.setFont (mono (9.0f));
        g.setColour (romanCol.withAlpha (degOpacity));
        g.drawText (ch.roman,
                    juce::Rectangle<int> (r.getX(), romanY + (int) degDy, r.getWidth(), 12),
                    juce::Justification::centred);

        // Chord name with reveal animation taking the spotlight.
        g.setFont (sans (16.0f, true));
        g.setColour (LofiC::INK_HERO.withAlpha (juce::jmax (nameOpacity, 0.75f)));
        g.drawText (ch.name,
                    juce::Rectangle<int> (r.getX(), nameY + (int) nameDy, r.getWidth(), 18),
                    juce::Justification::centred);

        // Quality fades along with the resting text.
        g.setFont (mono (8.0f));
        g.setColour (qualCol.withAlpha (degOpacity));
        g.drawText (ch.qualLabel,
                    juce::Rectangle<int> (r.getX(), qualY, r.getWidth(), 12),
                    juce::Justification::centred);
    }
    else
    {
        g.setFont (mono (9.0f));
        g.setColour (romanCol);
        g.drawText (ch.roman,
                    juce::Rectangle<int> (r.getX(), romanY, r.getWidth(), 12),
                    juce::Justification::centred);

        g.setFont (sans (15.0f, true));
        g.setColour (nameCol);
        g.drawText (ch.name,
                    juce::Rectangle<int> (r.getX(), nameY, r.getWidth(), 18),
                    juce::Justification::centred);

        g.setFont (mono (8.0f));
        g.setColour (qualCol);
        g.drawText (ch.qualLabel,
                    juce::Rectangle<int> (r.getX(), qualY, r.getWidth(), 12),
                    juce::Justification::centred);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// STATUS BAR  (y=540, h=24, #0F0E0B)
// ═══════════════════════���════════════════════════════════════��════════════

void FormaEditor::drawStatusBar (juce::Graphics& g)
{
    auto r = juce::Rectangle<int> (0, 540, getWidth(), 24);
    g.setColour (BG4);
    g.fillRect (r);
    g.setColour (BORDER);
    g.drawHorizontalLine (r.getY(), 0.0f, (float) getWidth());

    g.setFont (mono (9.0f));

    // Left: chord name
    g.setColour (ACCENT);
    g.drawText (statusChord, r.withTrimmedLeft (14).withWidth (200), juce::Justification::centredLeft);

    // Center: progression
    if (proc.currentProgressionName.isNotEmpty())
    {
        g.setColour (ACCENT_DK);
        g.drawText (proc.currentProgressionName, r, juce::Justification::centred);
    }

    // Right: key suggestion or context
    if (proc.keySuggestionActive && proc.keySuggestion.isNotEmpty())
    {
        g.setColour (ACCENT_DK);
        g.drawText (proc.keySuggestion, r.withTrimmedRight (14), juce::Justification::centredRight);
    }
    else
    {
        const char* syncNames[] = { "full sync", "expressive", "harmonic", "free" };
        g.setColour (TXT_GHOST);
        juce::String info = juce::String (juce::CharPointer_UTF8 (kMoods[moodIdx].name))
                            + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                            + keyNames[keyIdx]
                            + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                            + syncNames[currentSyncMode];
        g.drawText (info, r.withTrimmedRight (14), juce::Justification::centredRight);
    }
}

// ═══��═════════════════════════════════════════════════════════════════════
// ADVANCED OVERLAY
// ══════════════════════════��════════════════════════════════��═════════════

void FormaEditor::drawAdvanced (juce::Graphics& g)
{
    // Cover center column with semi-transparent bg
    g.setColour (juce::Colour (0xF51A1814));
    g.fillRect (centerCol);

    int px = centerCol.getX() + 30;
    int py = centerCol.getY() + 20;

    // Close button (X) top-right
    advCloseRect = juce::Rectangle<int> (centerCol.getRight() - 40, py, 24, 24);
    g.setFont (mono (14.0f));
    g.setColour (TXT_MID);
    g.drawText ("X", advCloseRect, juce::Justification::centred);

    // Title
    g.setFont (mono (10.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("ADVANCED", juce::Rectangle<int> (px, py, 200, 16), juce::Justification::centredLeft);
    py += 20;

    // ── PRESETS ──
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("PRESETS", juce::Rectangle<int> (px, py, 60, 12), juce::Justification::centredLeft);
    py += 13;

    int psW = 100, psH = 36, psGap = 4;
    for (int i = 0; i < 8; ++i)
    {
        int col = i % 4;
        int row = i / 4;
        presetSlotRects[i] = juce::Rectangle<int> (px + col * (psW + psGap),
                                                     py + row * (psH + psGap),
                                                     psW, psH);
        auto sr = presetSlotRects[i].toFloat();
        const auto& preset = proc.presets[i];
        bool isActive = (proc.currentPresetIndex == i);

        if (preset.isEmpty)
        {
            g.setColour (juce::Colour (0xFF1A1814));
            g.fillRoundedRectangle (sr, 6.0f);
            g.setColour (BORDER);
            g.drawRoundedRectangle (sr, 6.0f, 1.0f);
            g.setFont (mono (10.0f));
            g.setColour (TXT_GHOST);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), presetSlotRects[i], juce::Justification::centred);
        }
        else
        {
            g.setColour (isActive ? juce::Colour (0xFF242018) : juce::Colour (0xFF1E1C18));
            g.fillRoundedRectangle (sr, 6.0f);
            g.setColour (isActive ? ACCENT : juce::Colour (0xFF2A2820));
            g.drawRoundedRectangle (sr, 6.0f, 1.0f);

            // Preset name
            g.setFont (mono (9.0f));
            g.setColour (TXT_MID);
            g.drawText (preset.name,
                        presetSlotRects[i].withTrimmedBottom (18).withTrimmedTop (6),
                        juce::Justification::centred);

            // Mood name
            g.setFont (mono (8.0f));
            g.setColour (ACCENT);
            g.drawText (preset.mood,
                        presetSlotRects[i].withTrimmedTop (24),
                        juce::Justification::centred);
        }
    }
    py += 2 * (psH + psGap) + 4;

    // SOUND preset pills
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("SOUND", juce::Rectangle<int> (px, py, 50, 12), juce::Justification::centredLeft);
    py += 13;

    const char* soundNames[] = { "KEYS", "FELT", "GLASS", "TAPE", "AMBIENT", "MALLET" };
    for (int i = 0; i < 6; ++i)
    {
        advSoundPills[i] = juce::Rectangle<int> (px + i * 80, py, 74, 18);
        drawPill (g, advSoundPills[i], soundNames[i], currentSoundPreset == i);
    }
    py += 24;

    // VOICING pills
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("VOICING", juce::Rectangle<int> (px, py, 60, 12), juce::Justification::centredLeft);
    py += 13;
    const char* voicingNames[] = { "FULL", "UPPER", "SHELL" };
    for (int i = 0; i < 3; ++i)
    {
        advVoicingPills[i] = juce::Rectangle<int> (px + i * 70, py, 64, 18);
        drawPill (g, advVoicingPills[i], voicingNames[i], currentVoicingMode == i);
    }
    py += 24;

    // SYNC row (OUTPUT row removed — voice routing is now driven by the
    // selected top-level tab, not by a separate toggle.)
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("SYNC", juce::Rectangle<int> (px, py, 40, 12), juce::Justification::centredLeft);
    py += 13;

    const char* syncLabels[] = { "FULL", "EXPR", "HARM", "FREE" };
    for (int i = 0; i < 4; ++i)
    {
        advSyncPills[i] = juce::Rectangle<int> (px + i * 58, py, 52, 20);
        drawPill (g, advSyncPills[i], syncLabels[i], currentSyncMode == i);
    }
    py += 28;

    // ── LAYERS ──
    g.setColour (BORDER);
    g.drawHorizontalLine (py, (float) px, (float)(px + 540));
    py += 10;

    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("LAYERS", juce::Rectangle<int> (px, py, 60, 12), juce::Justification::centredLeft);
    py += 16;

    // CHORDS card (always on)
    {
        auto cardR = juce::Rectangle<int> (px, py, 520, 30);
        g.setColour (juce::Colour (0xFF1A1814));
        g.fillRoundedRectangle (cardR.toFloat(), 6.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (cardR.toFloat(), 6.0f, 1.0f);
        g.setFont (mono (9.0f));
        g.setColour (ACCENT);
        g.drawText ("CHORDS", cardR.withTrimmedLeft (12), juce::Justification::centredLeft);
        // "always on" tag
        auto tagR = juce::Rectangle<int> (px + 80, py + 8, 56, 14);
        g.setColour (juce::Colour (0xFF141210));
        g.fillRoundedRectangle (tagR.toFloat(), 3.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (tagR.toFloat(), 3.0f, 0.5f);
        g.setFont (mono (7.0f));
        g.setColour (TXT_GHOST);
        g.drawText ("always on", tagR, juce::Justification::centred);
    }
    py += 36;

    // ── SYNTH + SUGGESTIONS (combined row) ──
    g.setColour (BORDER);
    g.drawHorizontalLine (py, (float) px, (float)(px + 540));
    py += 10;

    // SYNTH VOL (left)
    g.setFont (mono (8.0f));
    g.setColour (TXT_MID);
    g.drawText ("SYNTH VOL", juce::Rectangle<int> (px, py, 70, 12), juce::Justification::centredLeft);
    g.drawText (juce::String ((int)(synthVol * 100)) + "%",
                juce::Rectangle<int> (px + 160, py, 40, 12), juce::Justification::centredLeft);

    // SUGGESTIONS label + toggle (right)
    g.drawText ("SUGGESTIONS", juce::Rectangle<int> (px + 300, py, 80, 12), juce::Justification::centredLeft);
    py += 13;

    advSynthSlider = juce::Rectangle<int> (px, py, 150, 6);
    drawMiniSlider (g, advSynthSlider, synthVol);

    // Suggestions toggle + reset (right side)
    g.setFont (mono (9.0f));
    g.setColour (juce::Colour (0xFF7A7670));
    advSuggestionsToggleRect = juce::Rectangle<int> (px + 300, py - 2, 26, 15);
    drawToggle (g, advSuggestionsToggleRect, suggestionsOn);

    advSuggestionsResetRect = juce::Rectangle<int> (px + 340, py - 3, 100, 17);
    auto resetCol = (resetFlashTimer > 0.0f) ? ACCENT : BORDER;
    g.setColour (resetCol);
    g.drawRoundedRectangle (advSuggestionsResetRect.toFloat(), 4.0f, 1.0f);
    g.setFont (mono (9.0f));
    g.setColour ((resetFlashTimer > 0.0f) ? ACCENT : TXT_DIM);
    g.drawText ("RESET", advSuggestionsResetRect, juce::Justification::centred);
}

// ═══════════════════════���══════════════════════════════���══════════════════
// PILL / TOGGLE / STEPPER / SLIDER drawing helpers
// ═════════════════════════════��═════════════════════════════��═════════════

void FormaEditor::drawPill (juce::Graphics& g, juce::Rectangle<int> r,
                              const juce::String& text, bool active)
{
    g.setColour (active ? BG3 : BG2);
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    g.setColour (active ? ACCENT_DK : BORDER);
    g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.0f);
    g.setFont (mono (9.0f));
    g.setColour (active ? ACCENT : juce::Colour (0xFF7A7670));
    g.drawText (text, r, juce::Justification::centred);
}

void FormaEditor::drawStepper (juce::Graphics& g, juce::Rectangle<int> r,
                                 const char* label, int val)
{
    g.setFont (mono (9.0f));
    g.setColour (juce::Colour (0xFF7A7670));
    g.drawText (label, r.withWidth (30), juce::Justification::centredLeft);

    auto minR = juce::Rectangle<int> (r.getX() + 34, r.getY(), 17, 17);
    g.setColour (BG2);
    g.fillRoundedRectangle (minR.toFloat(), 3.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (minR.toFloat(), 3.0f, 1.0f);
    g.setFont (mono (10.0f));
    g.setColour (juce::Colour (0xFF7A7670));
    g.drawText ("-", minR, juce::Justification::centred);

    auto valR = juce::Rectangle<int> (r.getX() + 55, r.getY(), 30, 17);
    g.setColour (BG4);
    g.fillRoundedRectangle (valR.toFloat(), 2.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (valR.toFloat(), 2.0f, 1.0f);
    g.setFont (mono (11.0f));
    g.setColour (juce::Colour (0xFFB8B4AB));
    juce::String vs = (val > 0 ? "+" : "") + juce::String (val);
    g.drawText (vs, valR, juce::Justification::centred);

    auto plR = juce::Rectangle<int> (r.getX() + 89, r.getY(), 17, 17);
    g.setColour (BG2);
    g.fillRoundedRectangle (plR.toFloat(), 3.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (plR.toFloat(), 3.0f, 1.0f);
    g.setFont (mono (10.0f));
    g.setColour (juce::Colour (0xFF7A7670));
    g.drawText ("+", plR, juce::Justification::centred);
}

void FormaEditor::drawToggle (juce::Graphics& g, juce::Rectangle<int> r, bool on)
{
    auto rf = r.toFloat();
    g.setColour (BG2);
    g.fillRoundedRectangle (rf, 8.0f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (rf, 8.0f, 1.0f);

    float ks = 11.0f;
    float kx = on ? rf.getRight() - ks - 2 : rf.getX() + 2;
    float ky = rf.getY() + (rf.getHeight() - ks) * 0.5f;

    g.setColour (on ? ACCENT : TXT_GHOST);
    g.fillEllipse (kx, ky, ks, ks);
    if (on)
    {
        g.setColour (ACCENT.withAlpha (0.3f));
        g.fillEllipse (kx - 3, ky - 3, ks + 6, ks + 6);
    }
}

void FormaEditor::drawMiniSlider (juce::Graphics& g, juce::Rectangle<int> r, float val)
{
    auto tr = juce::Rectangle<float> ((float) r.getX(), (float)(r.getY() + 1), (float) r.getWidth(), 3.0f);
    g.setColour (BG4);
    g.fillRoundedRectangle (tr, 1.5f);
    g.setColour (BORDER);
    g.drawRoundedRectangle (tr, 1.5f, 0.5f);

    auto fill = tr.withWidth (tr.getWidth() * val);
    g.setColour (ACCENT);
    g.fillRoundedRectangle (fill, 1.5f);

    g.setColour (ACCENT.withAlpha (0.2f));
    g.fillRoundedRectangle (fill.expanded (0, 2), 2.0f);
}

// ══════════���═════════════════════════════════════��════════════════════════
// MOUSE HANDLING
// ══════��═══════════════════���═══════════════════════════��══════════════════

void FormaEditor::mouseMove (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    // Mood hover
    int newHoveredMood = -1;
    int moodStartY = leftCol.getY() + 20 + 18;
    int moodPx = leftCol.getX() + 15;
    for (int i = 0; i < kNumMoods; ++i)
    {
        int yOff = i * 25 + (i >= 8 ? 18 : 0);  // separator offset for pack moods
        auto mr = juce::Rectangle<int> (moodPx, moodStartY + yOff, 123, 23);
        if (mr.contains (pos)) { newHoveredMood = i; break; }
    }
    hoveredMoodIdx = newHoveredMood;

    // Chord hover
    int newHoveredChord = -1;
    for (int i = 0; i < 7; ++i)
    {
        if (chordKeyRects[i].contains (pos)) { newHoveredChord = i; break; }
    }
    hoveredChordIdx = newHoveredChord;
}

void FormaEditor::mouseDown (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    // ── Advanced overlay interactions ──
    if (advancedVisible)
    {
        // Close button
        if (advCloseRect.contains (pos)) { advancedVisible = false; repaint(); return; }

        // Voicing pills
        for (int i = 0; i < 3; ++i)
        {
            if (advVoicingPills[i].contains (pos))
            {
                currentVoicingMode = i;
                proc.voicingMode.store (i);
                repaint();
                return;
            }
        }

        // Sound preset pills
        for (int i = 0; i < 6; ++i)
        {
            if (advSoundPills[i].contains (pos))
            {
                currentSoundPreset = i;
                proc.applySoundPreset (i);
                repaint();
                return;
            }
        }

        // (OUTPUT row removed — output routing is driven by the selected tab.)

        // Sync mode pills
        for (int i = 0; i < 4; ++i)
        {
            if (advSyncPills[i].contains (pos))
            {
                currentSyncMode = i;
                proc.syncMode.store (i);
                repaint();
                return;
            }
        }

        // Chord OCT stepper (only per-voice control still hosted here; the
        // rest of the bass/arp per-voice UI has moved to the tabs).
        if (advChordOctMinRect.contains (pos) && chordOctVal > -2) { chordOctVal--; proc.octaveChordParam.store (chordOctVal); repaint(); return; }
        if (advChordOctPlRect.contains (pos)  && chordOctVal < 2)  { chordOctVal++; proc.octaveChordParam.store (chordOctVal); repaint(); return; }

        // Synth vol slider
        if (advSynthSlider.expanded (0, 8).contains (pos))
        {
            synthVol = juce::jlimit (0.0f, 1.0f, (float)(pos.x - advSynthSlider.getX()) / (float) advSynthSlider.getWidth());
            proc.synthVolume.store (synthVol);
            repaint();
            return;
        }

        // Suggestions toggle
        if (advSuggestionsToggleRect.contains (pos))
        {
            suggestionsOn = !suggestionsOn;
            proc.suggestionsVisible.store (suggestionsOn);
            repaint();
            return;
        }

        // Suggestions reset
        if (advSuggestionsResetRect.contains (pos))
        {
            proc.manualHarmonicReset();
            resetFlashTimer = 0.2f;  // 200ms visual flash
            repaint();
            return;
        }

        // Preset slots
        for (int i = 0; i < 8; ++i)
        {
            if (presetSlotRects[i].contains (pos))
            {
                if (proc.presets[i].isEmpty)
                {
                    // Save current state to this slot
                    juce::String defaultName = proc.harmonyEngine.getCurrentMood() + " "
                                                + keyNames[keyIdx];
                    auto* aw = new juce::AlertWindow ("Save Preset",
                                                       "Name this preset:",
                                                       juce::AlertWindow::NoIcon);
                    aw->addTextEditor ("name", defaultName, "");
                    aw->addButton ("Save", 1);
                    aw->addButton ("Cancel", 0);
                    aw->enterModalState (true, juce::ModalCallbackFunction::create (
                        [this, i, aw] (int result)
                        {
                            if (result == 1)
                            {
                                auto name = aw->getTextEditorContents ("name");
                                if (name.isEmpty()) name = "Preset " + juce::String (i + 1);
                                proc.savePreset (i, name);
                            }
                            delete aw;
                            repaint();
                        }), false);
                }
                else
                {
                    if (e.mods.isPopupMenu())
                    {
                        // Right-click context menu
                        juce::PopupMenu menu;
                        menu.addItem (1, "Load");
                        menu.addItem (2, "Rename");
                        menu.addItem (3, "Clear");
                        menu.showMenuAsync (juce::PopupMenu::Options(),
                            [this, i] (int choice)
                            {
                                if (choice == 1)
                                {
                                    proc.loadPreset (i);
                                }
                                else if (choice == 2)
                                {
                                    auto* aw = new juce::AlertWindow ("Rename Preset",
                                                                       "New name:",
                                                                       juce::AlertWindow::NoIcon);
                                    aw->addTextEditor ("name", proc.presets[i].name, "");
                                    aw->addButton ("OK", 1);
                                    aw->addButton ("Cancel", 0);
                                    aw->enterModalState (true, juce::ModalCallbackFunction::create (
                                        [this, i, aw] (int result)
                                        {
                                            if (result == 1)
                                            {
                                                auto name = aw->getTextEditorContents ("name");
                                                if (name.isNotEmpty())
                                                    proc.presets[i].name = name;
                                            }
                                            delete aw;
                                            repaint();
                                        }), false);
                                }
                                else if (choice == 3)
                                {
                                    proc.presets[i] = FormaPreset();
                                    if (proc.currentPresetIndex == i)
                                        proc.currentPresetIndex = -1;
                                    repaint();
                                }
                            });
                    }
                    else
                    {
                        // Left click — load preset
                        proc.loadPreset (i);
                    }
                }
                repaint();
                return;
            }
        }

        // Click outside controls in overlay — do nothing (don't close)
        return;
    }

    // ── Gear button ─��
    if (gearBtnRect.contains (pos))
    {
        advancedVisible = !advancedVisible;
        repaint();
        return;
    }

    // ── XY Pad (circular bounds) ──
    {
        float dx = (float)(pos.x - xyPadCircle.getCentreX());
        float dy = (float)(pos.y - xyPadCircle.getCentreY());
        float padRad = xyPadCircle.getWidth() * 0.5f;
        if (dx * dx + dy * dy <= padRad * padRad)
        {
            draggingDot = true;
            float nx = dx / padRad;
            float ny = dy / padRad;
            dotX = (nx + 1.0f) * 0.5f;
            dotY = (ny + 1.0f) * 0.5f;
            float colorVal = 1.0f - dotY;
            float feelVal  = dotX;
            proc.colorAmount.store (colorVal);
            proc.feelAmount.store (feelVal);
            proc.harmonyEngine.setColorAmount (colorVal);
            proc.driftAmount.store (proc.getDriftForMoodAndFeel (feelVal));
            proc.xyDotX.store (dotX);
            proc.xyDotY.store (dotY);
            updateChordLabels();
            repaint();
            return;
        }
    }

    // ── Chord keys ──
    for (int i = 0; i < 7; ++i)
    {
        if (chordKeyRects[i].contains (pos))
        {
            pressedChordIdx = i;
            chords[i].pressed = true;
            // Kick the flash immediately on click — don't wait for the
            // processor's activeDegree atomic to round-trip through timer.
            pillAnim[i].state = PillAnim::State::Flashing;
            pillAnim[i].flashStartMs = juce::Time::getMillisecondCounterHiRes();
            proc.triggerChordFromEditor (i + 1);
            statusChord = proc.harmonyEngine.getChordName (i + 1);
            repaint();
            return;
        }
    }

    // ── Right column: CHORDS / BASS voice toggles ──
    if (rightChordsToggleRect.contains (pos))
    {
        chordsEnabledUI = ! chordsEnabledUI;
        proc.chordsEnabled.store (chordsEnabledUI);
        repaint();
        return;
    }
    if (rightBassToggleRect.contains (pos))
    {
        bassEnabledUI = ! bassEnabledUI;
        proc.bassEnabled.store (bassEnabledUI);
        repaint();
        return;
    }

    // Bass mode pills
    for (int i = 0; i < 3; ++i)
    {
        if (bassModePills[i].contains (pos))
        {
            bassModeUI = i;
            proc.bassMode.store (i);
            repaint();
            return;
        }
    }

    // Bass octave stepper
    if (bassOctMinRect.contains (pos) && bassOct > -2)
    {   bassOct--; proc.octaveBassParam.store (bassOct); repaint(); return; }
    if (bassOctPlRect.contains (pos)  && bassOct < 2)
    {   bassOct++; proc.octaveBassParam.store (bassOct); repaint(); return; }

    // Trigger note stepper (only when bass mode uses triggers)
    if (bassModeUI >= 1)
    {
        if (bassTrigNoteMinRect.contains (pos) && bassTrigNoteUI > 0)
        {   bassTrigNoteUI--; proc.bassTriggerNoteParam.store (bassTrigNoteUI); repaint(); return; }
        if (bassTrigNotePlRect.contains (pos) && bassTrigNoteUI < 127)
        {   bassTrigNoteUI++; proc.bassTriggerNoteParam.store (bassTrigNoteUI); repaint(); return; }
    }

    // Variation slider (only in Kick + Variation mode)
    if (bassModeUI == 2 && bassVariationSlider.expanded (0, 8).contains (pos))
    {
        float t = juce::jlimit (0.0f, 1.0f,
            (float)(pos.x - bassVariationSlider.getX()) / (float) bassVariationSlider.getWidth());
        bassVariationUI = t;
        proc.bassVariationAmount.store (bassVariationUI);
        repaint();
        return;
    }

    // ── Key arrows ──
    int bottomY = leftCol.getBottom() - 120 + 16 + 16;
    int kpx = leftCol.getX() + 15;
    auto downR = juce::Rectangle<int> (kpx, bottomY, 20, 20);
    auto upR   = juce::Rectangle<int> (kpx + 98, bottomY, 20, 20);
    if (downR.contains (pos)) { setKey ((keyIdx + 11) % 12); return; }
    if (upR.contains (pos))   { setKey ((keyIdx + 1) % 12);  return; }

    // ── OCT stepper (left column) ──
    if (advChordOctMinRect.contains (pos) && chordOctVal > -2) { chordOctVal--; proc.octaveChordParam.store (chordOctVal); repaint(); return; }
    if (advChordOctPlRect.contains (pos)  && chordOctVal < 2)  { chordOctVal++; proc.octaveChordParam.store (chordOctVal); repaint(); return; }

    // ── Mood list ��─
    int moodStartY = leftCol.getY() + 20 + 18;
    for (int i = 0; i < kNumMoods; ++i)
    {
        int yOff = i * 25 + (i >= 8 ? 18 : 0);
        auto mr = juce::Rectangle<int> (leftCol.getX() + 15, moodStartY + yOff, 123, 23);
        if (mr.contains (pos)) { setMood (i); return; }
    }
}

void FormaEditor::mouseDrag (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    if (draggingDot)
    {
        float padRad = xyPadCircle.getWidth() * 0.5f;
        float cx = (float) xyPadCircle.getCentreX();
        float cy = (float) xyPadCircle.getCentreY();
        float dx = (float) pos.x - cx;
        float dy = (float) pos.y - cy;

        // Clamp to circle boundary
        if (dx * dx + dy * dy > padRad * padRad)
        {
            float dist = std::sqrt (dx * dx + dy * dy);
            dx *= padRad / dist;
            dy *= padRad / dist;
        }

        float nx = dx / padRad;  // -1 to +1
        float ny = dy / padRad;  // -1 to +1 (y NOT inverted in screen)

        dotX = (nx + 1.0f) * 0.5f;
        dotY = (ny + 1.0f) * 0.5f;

        float colorVal = 1.0f - dotY;  // up = more color
        float feelVal  = dotX;          // right = more feel

        proc.colorAmount.store (colorVal);
        proc.feelAmount.store (feelVal);
        proc.harmonyEngine.setColorAmount (colorVal);
        proc.driftAmount.store (proc.getDriftForMoodAndFeel (feelVal));
        proc.xyDotX.store (dotX);
        proc.xyDotY.store (dotY);
        updateChordLabels();
        repaint();
    }

    // Advanced overlay slider drags
    if (advancedVisible)
    {
        if (advSynthSlider.expanded (0, 12).contains (pos))
        {
            synthVol = juce::jlimit (0.0f, 1.0f, (float)(pos.x - advSynthSlider.getX()) / (float) advSynthSlider.getWidth());
            proc.synthVolume.store (synthVol);
            repaint();
        }
    }
}

void FormaEditor::mouseUp (const juce::MouseEvent&)
{
    if (pressedChordIdx >= 0)
    {
        chords[pressedChordIdx].pressed = false;
        proc.releaseChordFromEditor();
        pressedChordIdx = -1;
        repaint();
    }
    draggingDot = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// RIGHT COLUMN — voice toggles + compact bass section
// ═══════════════════════════════════════════════════════════════════════════

void FormaEditor::drawRightCol (juce::Graphics& g)
{
    g.setColour (BG2);
    g.fillRect (rightCol);
    g.setColour (BORDER);
    g.drawVerticalLine (rightCol.getX(), (float) rightCol.getY(), (float) rightCol.getBottom());

    const int px = rightCol.getX() + 14;
    int py       = rightCol.getY() + 22;

    // ── CHORDS toggle ──
    g.setFont (mono (10.0f));
    g.setColour (chordsEnabledUI ? TXT_HI : TXT_DIM);
    g.drawText ("CHORDS", juce::Rectangle<int> (px, py, 70, 14), juce::Justification::centredLeft);
    rightChordsToggleRect = juce::Rectangle<int> (rightCol.getRight() - 14 - 26, py, 26, 14);
    drawToggle (g, rightChordsToggleRect, chordsEnabledUI);
    py += 26;

    // ── BASS toggle ──
    g.setColour (bassEnabledUI ? TXT_HI : TXT_DIM);
    g.drawText ("BASS", juce::Rectangle<int> (px, py, 70, 14), juce::Justification::centredLeft);
    rightBassToggleRect = juce::Rectangle<int> (rightCol.getRight() - 14 - 26, py, 26, 14);
    drawToggle (g, rightBassToggleRect, bassEnabledUI);
    py += 28;

    // Divider
    g.setColour (BORDER);
    g.drawHorizontalLine (py, (float)(rightCol.getX() + 10), (float)(rightCol.getRight() - 10));
    py += 14;

    // ── BASS section header ──
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("BASS", juce::Rectangle<int> (px, py, 80, 10), juce::Justification::centredLeft);
    py += 14;

    // Mode pills stacked vertically
    static const char* modeNames[] = { "ROOT", "KICK TRIGGER", "KICK + VAR" };
    const int pillW = rightCol.getWidth() - 28;
    for (int i = 0; i < 3; ++i)
    {
        bassModePills[i] = juce::Rectangle<int> (px, py + i * 26, pillW, 22);
        drawPill (g, bassModePills[i], modeNames[i], bassModeUI == i);
    }
    py += 3 * 26 + 8;

    // OCT stepper
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("OCT", juce::Rectangle<int> (px, py, 40, 10), juce::Justification::centredLeft);
    {
        int sy = py + 12;
        bassOctMinRect = juce::Rectangle<int> (px,      sy, 17, 17);
        bassOctPlRect  = juce::Rectangle<int> (px + 55, sy, 17, 17);

        g.setColour (BG4);
        g.fillRoundedRectangle (bassOctMinRect.toFloat(), 3.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (bassOctMinRect.toFloat(), 3.0f, 1.0f);
        g.setFont (mono (10.0f));
        g.setColour (TXT_MID);
        g.drawText ("-", bassOctMinRect, juce::Justification::centred);

        auto valR = juce::Rectangle<int> (px + 20, sy, 32, 17);
        g.setColour (BG4);
        g.fillRoundedRectangle (valR.toFloat(), 2.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (valR.toFloat(), 2.0f, 1.0f);
        g.setFont (mono (10.0f));
        g.setColour (juce::Colour (0xFFB8B4AB));
        juce::String vs = (bassOct > 0 ? "+" : "") + juce::String (bassOct);
        g.drawText (vs, valR, juce::Justification::centred);

        g.setColour (BG4);
        g.fillRoundedRectangle (bassOctPlRect.toFloat(), 3.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (bassOctPlRect.toFloat(), 3.0f, 1.0f);
        g.setFont (mono (10.0f));
        g.setColour (TXT_MID);
        g.drawText ("+", bassOctPlRect, juce::Justification::centred);
    }
    py += 34;

    // Trigger note (only in modes 1/2)
    if (bassModeUI >= 1)
    {
        g.setFont (mono (8.0f));
        g.setColour (TXT_GHOST);
        g.drawText ("TRIGGER NOTE", juce::Rectangle<int> (px, py, 120, 10), juce::Justification::centredLeft);
        int sy = py + 12;
        bassTrigNoteMinRect = juce::Rectangle<int> (px,              sy, 17, 17);
        bassTrigNotePlRect  = juce::Rectangle<int> (px + pillW - 17, sy, 17, 17);

        g.setColour (BG4);
        g.fillRoundedRectangle (bassTrigNoteMinRect.toFloat(), 3.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (bassTrigNoteMinRect.toFloat(), 3.0f, 1.0f);
        g.setFont (mono (10.0f));
        g.setColour (TXT_MID);
        g.drawText ("-", bassTrigNoteMinRect, juce::Justification::centred);

        auto valR = juce::Rectangle<int> (px + 20, sy, pillW - 40, 17);
        g.setColour (BG4);
        g.fillRoundedRectangle (valR.toFloat(), 2.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (valR.toFloat(), 2.0f, 1.0f);
        g.setFont (mono (10.0f));
        g.setColour (juce::Colour (0xFFB8B4AB));
        static const char* const pcs[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        int n = juce::jlimit (0, 127, bassTrigNoteUI);
        juce::String noteStr = juce::String (pcs[n % 12]) + juce::String ((n / 12) - 2);
        g.drawText (noteStr, valR, juce::Justification::centred);

        g.setColour (BG4);
        g.fillRoundedRectangle (bassTrigNotePlRect.toFloat(), 3.0f);
        g.setColour (BORDER);
        g.drawRoundedRectangle (bassTrigNotePlRect.toFloat(), 3.0f, 1.0f);
        g.setFont (mono (10.0f));
        g.setColour (TXT_MID);
        g.drawText ("+", bassTrigNotePlRect, juce::Justification::centred);

        py += 34;
    }

    // Variation slider (only in mode 2)
    if (bassModeUI == 2)
    {
        g.setFont (mono (8.0f));
        g.setColour (TXT_GHOST);
        g.drawText ("VARIATION", juce::Rectangle<int> (px, py, 80, 10), juce::Justification::centredLeft);
        g.setColour (TXT_MID);
        g.drawText (juce::String ((int)(bassVariationUI * 100)) + "%",
                    juce::Rectangle<int> (px + pillW - 40, py, 40, 10),
                    juce::Justification::centredRight);
        bassVariationSlider = juce::Rectangle<int> (px, py + 14, pillW, 6);
        drawMiniSlider (g, bassVariationSlider, bassVariationUI);
    }
}

