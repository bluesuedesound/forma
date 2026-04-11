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

// ══════════════════���════════════════════════════��═════════════════════════
// CONSTRUCTOR
// ══════════��════════════════��═════════════════════���═══════════════════════

FormaEditor::FormaEditor (FormaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (780, 564);
    currentOutputMode = proc.outputMode.load();
    currentSyncMode = proc.syncMode.load();
    currentBgColor = juce::Colour (kMoods[0].bgColor);
    targetBgColor  = currentBgColor;

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
        for (int i = 0; i < 7; ++i)
            chords[i].pressed = (i + 1 == deg);
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

    // ── Thermal wave animation ──
    lastKnownDeg = deg;
    lastKnownArp = proc.lastArpNote.load();

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

    // Sync editor state from processor
    arpGateVal = proc.arpGateParam.load();
    bassAlt    = proc.bassAltParam.load();
    bassOn     = proc.bassEnabledParam.load();

    float r = proc.arpRate.load();
    if      (r >= 1.5f)  arpRate = 2;
    else if (r >= 0.75f) arpRate = 1;
    else                 arpRate = 0;

    arpPattern = (int) proc.arpeggiator.getPattern();

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

    // Bass
    bassOn  = proc.bassEnabledParam.load();
    bassAlt = proc.bassAltParam.load();
    bassOct = proc.octaveBassParam.load();

    // Arp
    arpOnState = proc.arpEnabled.load();
    arpPattern = (int) proc.arpeggiator.getPattern();
    float r = proc.arpeggiator.getRate();
    if      (r >= 1.5f)  arpRate = 2;
    else if (r >= 0.75f) arpRate = 1;
    else                 arpRate = 0;
    arpGateVal = proc.arpeggiator.getGate();
    arpSpread  = proc.arpeggiator.getSpread() - 1;  // 1->0, 2->1
    arpOct     = proc.arpeggiator.getOctaveOffset();

    // Output/Sync
    currentOutputMode = proc.outputMode.load();
    currentSyncMode   = proc.syncMode.load();

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
    leftCol   = juce::Rectangle<int> (0, 40, 155, 500);
    centerCol = juce::Rectangle<int> (155, 40, 625, 500);

    // XY compass container (380x380 centered in circle area which is top 320px of center)
    int circleAreaH = centerCol.getHeight() - 180; // 320px for circle area
    int compSize = 380;
    int compX = centerCol.getX() + (centerCol.getWidth() - compSize) / 2;
    int compY = centerCol.getY() + (circleAreaH - compSize) / 2;
    compassContainer = juce::Rectangle<int> (compX, compY, compSize, compSize);

    // XY pad circle: 280x280 centered in compass container (50px margin for labels)
    int padSize = 280;
    int padX = compassContainer.getCentreX() - padSize / 2;
    int padY = compassContainer.getCentreY() - padSize / 2;
    xyPadCircle = juce::Rectangle<int> (padX, padY, padSize, padSize);

    // Chord keys: 7 equal width, gap=5, in bottom 180px of center
    int ckY = centerCol.getY() + circleAreaH;
    int ckH = 160;
    int ckGap = 5;
    int ckTotalW = centerCol.getWidth();
    int ckW = (ckTotalW - ckGap * 6) / 7;
    for (int i = 0; i < 7; ++i)
        chordKeyRects[i] = juce::Rectangle<int> (centerCol.getX() + i * (ckW + ckGap), ckY + 14, ckW, ckH);

    g.fillAll (BG4);

    drawTopBar (g);
    drawLeftCol (g);
    drawCenter (g);
    drawStatusBar (g);

    if (advancedVisible)
        drawAdvanced (g);
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

    // Chord keys divider
    int circleAreaH = centerCol.getHeight() - 180;
    int divY = centerCol.getY() + circleAreaH;
    g.setColour (BORDER);
    g.drawHorizontalLine (divY, (float) centerCol.getX(), (float) centerCol.getRight());

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

    // 1. Background
    g.setColour (juce::Colour (mt.voidCol));
    g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // 2. Thermal gradient from dot outward
    float vdist = std::sqrt ((dpx - cx) * (dpx - cx) + (dpy - cy) * (dpy - cy));
    float gR = radius + vdist * 0.8f + radius * 0.15f;

    juce::ColourGradient grad (juce::Colour (mt.voidCol), { dpx, dpy },
                                juce::Colour (mt.edge), { dpx + gR, dpy }, true);
    grad.addColour (0.10, juce::Colour (mt.inner));
    grad.addColour (0.26, juce::Colour (mt.mid1));
    grad.addColour (0.48, juce::Colour (mt.mid2));
    grad.addColour (0.70, juce::Colour (mt.outer));
    grad.addColour (0.90, juce::Colour (mt.edge));
    g.setGradientFill (grad);
    g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

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
    auto& mt = kMoodThermals[juce::jlimit (0, kNumMoods - 1, currentThermalIdx)];
    g.setColour (juce::Colour (0xFF060504));
    g.fillEllipse (dpx - 5.0f, dpy - 5.0f, 10.0f, 10.0f);
    g.setColour (juce::Colour (mt.dotStroke));
    juce::Path dp; dp.addEllipse (dpx - 5.0f, dpy - 5.0f, 10.0f, 10.0f);
    g.strokePath (dp, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (mt.dotStroke).withAlpha (0.55f));
    g.fillEllipse (dpx - 1.2f, dpy - 1.2f, 2.4f, 2.4f);
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
    }

    // Dot outside clip scope — sits on ring
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
    float rad = 10.0f;

    // Suggestion dots above top cap
    if (ch.sug1)
    {
        float sx = (float) r.getCentreX(), sy = (float)(r.getY() - 8);
        float alpha = 0.6f + sugPulse * 0.4f;
        g.setColour (SUGGEST1.withAlpha (alpha * 0.4f));
        g.fillEllipse (sx - 8, sy - 8, 16, 16);
        g.setColour (SUGGEST1.withAlpha (alpha));
        g.fillEllipse (sx - 3, sy - 3, 6, 6);
    }
    else if (ch.sug2)
    {
        float sx = (float) r.getCentreX(), sy = (float)(r.getY() - 7);
        g.setColour (SUGGEST2.withAlpha (0.8f));
        g.fillEllipse (sx - 3, sy - 3, 6, 6);
        g.setColour (SUGGEST2.withAlpha (0.3f));
        g.fillEllipse (sx - 6, sy - 6, 12, 12);
    }

    auto rf = r.toFloat();
    bool isHovered = (idx == hoveredChordIdx && !ch.pressed);

    if (ch.pressed)
    {
        // Top cap pressed
        auto cap = rf.withHeight (18);
        g.setColour (juce::Colour (0xFF120F0D));
        g.fillRoundedRectangle (cap, rad);
        // Inset shadow on cap
        juce::ColourGradient capInset (juce::Colour (0xE0000000), cap.getX(), cap.getY(),
                                        juce::Colours::transparentBlack, cap.getX(), cap.getBottom(), false);
        g.setGradientFill (capInset);
        g.fillRoundedRectangle (cap, rad);

        // Body
        g.setColour (juce::Colour (0xFF2A2420));
        g.fillRoundedRectangle (rf.withTrimmedTop (16), 5.0f);
        g.setColour (juce::Colour (0xFF2A2420));
        g.fillRect (rf.getX() + 1, rf.getY() + 14, rf.getWidth() - 2, 6.0f);

        // Inset shadow on body
        juce::ColourGradient inset (juce::Colour (0xB0000000), rf.getX(), rf.getY(),
                                     juce::Colours::transparentBlack, rf.getX(), rf.getY() + 16, false);
        g.setGradientFill (inset);
        g.fillRoundedRectangle (rf.withHeight (18), rad);

        // Outer glow
        g.setColour (juce::Colour (0x26C8875A));
        g.drawRoundedRectangle (rf.expanded (2), rad + 2, 2.0f);

        g.setColour (ACCENT);
        g.drawRoundedRectangle (rf, rad, 1.0f);
    }
    else
    {
        // Top cap idle/hover
        auto cap = rf.withHeight (18);
        g.setColour (juce::Colour (0xFF161412));
        g.fillRoundedRectangle (cap, rad);

        // Cap bottom border
        g.setColour (juce::Colour (0xFF1E1C18));
        g.drawHorizontalLine ((int)(rf.getY() + 17), rf.getX() + 4, rf.getRight() - 4);

        // Body
        g.setColour (isHovered ? juce::Colour (0xFF232018) : BG3);
        g.fillRoundedRectangle (rf.withTrimmedTop (16), 5.0f);
        g.setColour (isHovered ? juce::Colour (0xFF232018) : BG3);
        g.fillRect (rf.getX() + 1, rf.getY() + 14, rf.getWidth() - 2, 6.0f);

        g.setColour (isHovered ? juce::Colour (0xFF333028) : juce::Colour (0xFF262420));
        g.drawRoundedRectangle (rf, rad, 1.0f);
    }

    // Quality stripe (left edge, 2px)
    if (ch.qual == Minor)
    { g.setColour (juce::Colour (0xFF2A2820)); g.fillRect (rf.getX() + 1, rf.getY() + 12, 2.0f, rf.getHeight() - 24); }
    else if (ch.qual == Diminished)
    { g.setColour (ACCENT_DK); g.fillRect (rf.getX() + 1, rf.getY() + 12, 2.0f, rf.getHeight() - 24); }
    else if (ch.qual == Augmented)
    { g.setColour (juce::Colour (0xFF2A4A5A)); g.fillRect (rf.getX() + 1, rf.getY() + 12, 2.0f, rf.getHeight() - 24); }

    // Text — bottom aligned, pb=18px
    int textBot = r.getBottom() - 18;

    // Roman numeral
    g.setFont (mono (9.0f));
    g.setColour (ch.pressed ? juce::Colour (0xFF6B6760) : juce::Colour (0xFF2E2C28));
    g.drawText (ch.roman, juce::Rectangle<int> (r.getX(), textBot - 36, r.getWidth(), 12), juce::Justification::centred);

    // Chord name
    g.setFont (sans (15.0f, true));
    g.setColour (ch.pressed ? TXT_HI : juce::Colour (0xFF6A6660));
    g.drawText (ch.name, juce::Rectangle<int> (r.getX(), textBot - 22, r.getWidth(), 18), juce::Justification::centred);

    // Quality
    g.setFont (mono (8.0f));
    g.setColour (ch.pressed ? juce::Colour (0xFF6B6760) : TXT_DARK);
    g.drawText (ch.qualLabel, juce::Rectangle<int> (r.getX(), textBot - 2, r.getWidth(), 12), juce::Justification::centred);
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

    // OUTPUT + SYNC (combined row)
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("OUTPUT", juce::Rectangle<int> (px, py, 50, 12), juce::Justification::centredLeft);
    g.drawText ("SYNC", juce::Rectangle<int> (px + 290, py, 40, 12), juce::Justification::centredLeft);
    py += 13;

    const char* outLabels[] = { "ALL", "CHORDS", "BASS", "ARP" };
    for (int i = 0; i < 4; ++i)
    {
        advOutputPills[i] = juce::Rectangle<int> (px + i * 64, py, 58, 20);
        drawPill (g, advOutputPills[i], outLabels[i], currentOutputMode == i);
    }
    const char* syncLabels[] = { "FULL", "EXPR", "HARM", "FREE" };
    for (int i = 0; i < 4; ++i)
    {
        advSyncPills[i] = juce::Rectangle<int> (px + 270 + i * 58, py, 52, 20);
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

    // BASS card
    {
        bool bassOpen = bassOn;
        int cardH = bassOpen ? 70 : 30;
        auto cardR = juce::Rectangle<int> (px, py, 520, cardH);
        g.setColour (juce::Colour (0xFF1A1814));
        g.fillRoundedRectangle (cardR.toFloat(), 6.0f);
        g.setColour (bassOpen ? juce::Colour (0xFF2A2820) : BORDER);
        g.drawRoundedRectangle (cardR.toFloat(), 6.0f, 1.0f);
        g.setFont (mono (9.0f));
        g.setColour (TXT_MID);
        g.drawText ("BASS", cardR.withTrimmedLeft (12), juce::Justification::centredLeft);
        advBassOnRect = juce::Rectangle<int> (px + 490, py + 7, 26, 15);
        drawToggle (g, advBassOnRect, bassOn);

        if (bassOpen)
        {
            int by = py + 34;
            g.setFont (mono (9.0f));
            g.setColour (juce::Colour (0xFF7A7670));
            advBassOctMinRect = juce::Rectangle<int> (px + 46, by, 17, 17);
            advBassOctPlRect  = juce::Rectangle<int> (px + 101, by, 17, 17);
            drawStepper (g, juce::Rectangle<int> (px + 12, by, 120, 18), "OCT", bassOct);

            g.drawText ("ALT", juce::Rectangle<int> (px + 160, by, 28, 15), juce::Justification::centredLeft);
            advBassAltRect = juce::Rectangle<int> (px + 192, by, 26, 15);
            drawToggle (g, advBassAltRect, bassAlt);
        }
        py += cardH + 6;
    }

    // ── ARP section ──
    g.setColour (BORDER);
    g.drawHorizontalLine (py, (float) px, (float)(px + 540));
    py += 10;

    g.setFont (mono (8.0f));
    g.setColour (TXT_MID);
    g.drawText ("ARP", juce::Rectangle<int> (px, py, 40, 12), juce::Justification::centredLeft);

    // ON toggle (inline with label)
    g.setFont (mono (9.0f));
    g.setColour (juce::Colour (0xFF7A7670));
    advArpOnRect = juce::Rectangle<int> (px + 40, py - 1, 26, 15);
    drawToggle (g, advArpOnRect, arpOnState);
    py += 16;

    // MOTIF pills (2 rows of 3)
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("MOTIF", juce::Rectangle<int> (px, py, 50, 12), juce::Justification::centredLeft);
    py += 13;

    const char* motifs[] = { "Rise", "Cascade", "Pulse", "Groove", "Spiral", "Drift" };
    for (int i = 0; i < 6; ++i)
    {
        int col2 = i % 3;
        int row2 = i / 3;
        advMotifPills[i] = juce::Rectangle<int> (px + col2 * 88, py + row2 * 22, 82, 18);
        drawPill (g, advMotifPills[i], motifs[i], i == arpPattern);
    }
    py += 48;

    // RATE + SPREAD + GATE + OCT (compact rows)
    g.setFont (mono (8.0f));
    g.setColour (TXT_GHOST);
    g.drawText ("RATE", juce::Rectangle<int> (px, py, 40, 12), juce::Justification::centredLeft);
    g.drawText ("SPREAD", juce::Rectangle<int> (px + 180, py, 50, 12), juce::Justification::centredLeft);
    py += 13;

    const char* rates[] = { "\xc2\xbd", "1", "2\xc3\x97" };
    for (int i = 0; i < 3; ++i)
    {
        advRatePills[i] = juce::Rectangle<int> (px + i * 50, py, 44, 18);
        drawPill (g, advRatePills[i], rates[i], i == arpRate);
    }
    const char* spreads[] = { "1", "2" };
    for (int i = 0; i < 2; ++i)
    {
        advSpreadPills[i] = juce::Rectangle<int> (px + 180 + i * 40, py, 36, 18);
        drawPill (g, advSpreadPills[i], spreads[i], arpSpread == i);
    }

    // GATE inline with rate/spread row
    g.setColour (TXT_GHOST);
    g.drawText ("GATE", juce::Rectangle<int> (px + 300, py - 13, 40, 12), juce::Justification::centredLeft);
    advGateSlider = juce::Rectangle<int> (px + 300, py + 4, 130, 6);
    drawMiniSlider (g, advGateSlider, arpGateVal);
    g.setColour (TXT_MID);
    g.drawText (juce::String ((int)(arpGateVal * 100)) + "%",
                juce::Rectangle<int> (px + 436, py - 13, 40, 12), juce::Justification::centredLeft);
    py += 24;

    // ARP OCT stepper
    advArpOctMinRect = juce::Rectangle<int> (px + 34, py, 17, 17);
    advArpOctPlRect  = juce::Rectangle<int> (px + 89, py, 17, 17);
    drawStepper (g, juce::Rectangle<int> (px, py, 120, 18), "OCT", arpOct);
    py += 24;

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

        // Output mode pills
        for (int i = 0; i < 4; ++i)
        {
            if (advOutputPills[i].contains (pos))
            {
                currentOutputMode = i;  // 0=All, 1=Chords, 2=Bass, 3=Arp
                proc.outputMode.store (currentOutputMode);
                repaint();
                return;
            }
        }

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

        // Bass ON toggle
        if (advBassOnRect.contains (pos))
        {
            bassOn = !bassOn;
            proc.bassEnabledParam.store (bassOn);
            repaint();
            return;
        }

        // Bass ALT toggle
        if (advBassAltRect.contains (pos))
        {
            bassAlt = !bassAlt;
            proc.bassAltParam.store (bassAlt);
            repaint();
            return;
        }

        // Bass OCT stepper
        // Chord OCT stepper
        if (advChordOctMinRect.contains (pos) && chordOctVal > -2) { chordOctVal--; proc.octaveChordParam.store (chordOctVal); repaint(); return; }
        if (advChordOctPlRect.contains (pos)  && chordOctVal < 2)  { chordOctVal++; proc.octaveChordParam.store (chordOctVal); repaint(); return; }

        if (advBassOctMinRect.contains (pos) && bassOct > -2) { bassOct--; proc.octaveBassParam.store (bassOct); repaint(); return; }
        if (advBassOctPlRect.contains (pos)  && bassOct < 2)  { bassOct++; proc.octaveBassParam.store (bassOct); repaint(); return; }

        // Arp ON toggle
        if (advArpOnRect.contains (pos))
        {
            arpOnState = !arpOnState;
            proc.arpEnabled.store (arpOnState);
            if (arpOnState && proc.activeDegree.load() >= 1)
                proc.arpeggiator.setActive (true);
            else if (!arpOnState)
            {
                proc.arpeggiator.setActive (false);
                proc.arpeggiator.reset();
            }
            repaint();
            return;
        }

        // Motif pills
        for (int i = 0; i < 6; ++i)
        {
            if (advMotifPills[i].contains (pos))
            {
                arpPattern = i;
                static const Arpeggiator::Pattern pats[] = {
                    Arpeggiator::Pattern::Rise, Arpeggiator::Pattern::Cascade,
                    Arpeggiator::Pattern::Pulse, Arpeggiator::Pattern::Groove,
                    Arpeggiator::Pattern::Spiral, Arpeggiator::Pattern::Drift
                };
                proc.arpeggiator.setPattern (pats[i]);
                repaint();
                return;
            }
        }

        // Rate pills
        for (int i = 0; i < 3; ++i)
        {
            if (advRatePills[i].contains (pos))
            {
                arpRate = i;
                static const float rateValues[] = { 0.5f, 1.0f, 2.0f };
                proc.arpRate.store (rateValues[i]);
                repaint();
                return;
            }
        }

        // Spread pills
        for (int i = 0; i < 2; ++i)
        {
            if (advSpreadPills[i].contains (pos))
            {
                arpSpread = i;
                proc.arpSpread.store (i + 1);
                repaint();
                return;
            }
        }

        // Gate slider
        if (advGateSlider.expanded (0, 8).contains (pos))
        {
            arpGateVal = juce::jlimit (0.0f, 1.0f, (float)(pos.x - advGateSlider.getX()) / (float) advGateSlider.getWidth());
            proc.arpGateParam.store (arpGateVal);
            repaint();
            return;
        }

        // Arp OCT stepper
        if (advArpOctMinRect.contains (pos) && arpOct > -2) { arpOct--; proc.arpOctave.store (arpOct); repaint(); return; }
        if (advArpOctPlRect.contains (pos)  && arpOct < 2)  { arpOct++; proc.arpOctave.store (arpOct); repaint(); return; }

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
    float dx = (float)(pos.x - xyPadCircle.getCentreX());
    float dy = (float)(pos.y - xyPadCircle.getCentreY());
    float padRad = xyPadCircle.getWidth() * 0.5f;
    if (dx * dx + dy * dy <= padRad * padRad)
    {
        draggingDot = true;
        // Normalize to -1..+1 then to circle-clamped 0..1
        float nx = dx / padRad;
        float ny = dy / padRad;
        // Already inside circle (checked above)
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

    // ── Chord keys ──
    for (int i = 0; i < 7; ++i)
    {
        if (chordKeyRects[i].contains (pos))
        {
            pressedChordIdx = i;
            chords[i].pressed = true;
            proc.triggerChordFromEditor (i + 1);
            statusChord = proc.harmonyEngine.getChordName (i + 1);
            repaint();
            return;
        }
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
        if (advGateSlider.expanded (0, 12).contains (pos))
        {
            arpGateVal = juce::jlimit (0.0f, 1.0f, (float)(pos.x - advGateSlider.getX()) / (float) advGateSlider.getWidth());
            proc.arpGateParam.store (arpGateVal);
            repaint();
        }
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
