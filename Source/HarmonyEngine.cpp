#include "HarmonyEngine.h"

// ── Static note name arrays ────────────────────────────────────────────────

const juce::String HarmonyEngine::notesSharp[12] =
    { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

const juce::String HarmonyEngine::notesFlat[12] =
    { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };

// ── Mood names ─────────────────────────────────────────────────────────────

const juce::StringArray HarmonyEngine::moodNames =
    { "Bright", "Warm", "Dream", "Deep", "Hollow", "Tender", "Tense", "Dusk" };

// ── Mood intervals ─────────────────────────────────────────────────────────

struct MoodData {
    const char* name;
    int intervals[7];
    const char* qualities[7];
};

static const MoodData moods[] = {
    { "Bright",  {0,2,4,5,7,9,11},  {"M","m","m","M","M","m","d"} },
    { "Warm",    {0,2,4,5,7,9,10},  {"M","m","d","M","m","m","M"} },
    { "Dream",   {0,2,4,6,7,9,11},  {"M","M","m","d","M","m","m"} },
    { "Deep",    {0,2,3,5,7,9,10},  {"m","m","M","M","m","d","M"} },
    { "Hollow",  {0,2,3,5,7,8,10},  {"m","d","M","m","m","M","M"} },
    { "Tender",  {0,2,3,5,7,9,11},  {"m","m","A","M","M","d","d"} },
    { "Tense",   {0,2,3,5,7,8,11},  {"m","d","M","m","M","M","d"} },
    { "Dusk",    {0,2,3,5,7,9,10},  {"m","m","M","M","m","m","M"} },
};
static constexpr int numMoods = 8;

static const MoodData* findMood (const juce::String& name)
{
    for (int i = 0; i < numMoods; ++i)
        if (name == moods[i].name)
            return &moods[i];
    return nullptr;
}

// ── MOOD_COLORS ────────────────────────────────────────────────────────────

HarmonyEngine::ColorEntry HarmonyEngine::getColorEntry (
    const juce::String& mood, int degree)
{
    if (mood == "Bright")
    {
        switch (degree) {
            case 1: return {1, 2, 0.3f};   case 2: return {1, 2, 0.55f};
            case 3: return {1, 2, 0.75f};  case 4: return {1, 3, 0.6f};
            case 5: return {1, 2, 0.4f};   case 6: return {1, 2, 0.3f};
            case 7: return {1, 2, 0.9f};
        }
    }
    else if (mood == "Warm")
    {
        switch (degree) {
            case 1: return {1, 2, 0.25f};  case 2: return {1, 2, 0.55f};
            case 3: return {1, 1, 1.1f};   case 4: return {1, 2, 0.45f};
            case 5: return {1, 1, 1.1f};   case 6: return {1, 2, 0.5f};
            case 7: return {1, 2, 0.6f};
        }
    }
    else if (mood == "Dream")
    {
        switch (degree) {
            case 1: return {1, 3, 0.6f};   case 2: return {1, 2, 0.4f};
            case 3: return {1, 2, 0.55f};  case 4: return {1, 1, 1.1f};
            case 5: return {1, 2, 0.45f};  case 6: return {1, 2, 0.35f};
            case 7: return {1, 2, 0.7f};
        }
    }
    else if (mood == "Deep")
    {
        switch (degree) {
            case 1: return {1, 3, 0.3f};   case 2: return {1, 2, 0.45f};
            case 3: return {1, 3, 0.55f};  case 4: return {1, 3, 0.35f};
            case 5: return {1, 1, 1.1f};   case 6: return {1, 1, 1.1f};
            case 7: return {1, 2, 0.55f};
        }
    }
    else if (mood == "Hollow")
    {
        switch (degree) {
            case 1: return {1, 2, 0.7f};   case 2: return {1, 1, 1.1f};
            case 3: return {1, 2, 0.55f};  case 4: return {1, 2, 0.7f};
            case 5: return {1, 1, 1.1f};   case 6: return {1, 2, 0.45f};
            case 7: return {1, 2, 0.6f};
        }
    }
    else if (mood == "Tender")
    {
        switch (degree) {
            case 1: return {1, 3, 0.30f};  // triad -> Cm9
            case 2: return {1, 2, 0.30f};  // triad -> Dmaj7
            case 3: return {1, 2, 0.50f};  // triad -> EbMaj7#5
            case 4: return {1, 2, 0.30f};  // triad -> Fm7
            case 5: return {1, 2, 0.30f};  // triad -> G7
            case 6: return {1, 2, 0.50f};  // triad -> Am7b5
            case 7: return {1, 2, 0.50f};  // triad -> Bm7b5
        }
    }
    else if (mood == "Tense")
    {
        switch (degree) {
            case 1: return {1, 3, 0.7f};   case 2: return {1, 1, 1.1f};
            case 3: return {1, 2, 0.45f};  case 4: return {1, 2, 0.6f};
            case 5: return {1, 2, 0.3f};   case 6: return {1, 2, 0.5f};
            case 7: return {1, 2, 0.9f};
        }
    }
    else if (mood == "Dusk")
    {
        switch (degree) {
            case 1: return {1, 2, 0.30f};  // Cm -> Cm7 -> Cm9
            case 2: return {1, 2, 0.30f};  // Dm -> Dm7 -> Dm9
            case 3: return {1, 2, 0.30f};  // special handling in getChord
            case 4: return {1, 2, 0.30f};  // F -> Fmaj7
            case 5: return {1, 2, 0.30f};  // Gm -> Gm7 -> Gm9
            case 6: return {1, 2, 0.30f};  // Am -> Am7
            case 7: return {1, 2, 0.30f};  // Bb -> Bbmaj7
        }
    }

    return {1, 1, 1.1f};
}

// ── Triad intervals ────────────────────────────────────────────────────────

std::vector<int> HarmonyEngine::getTriad (const juce::String& quality)
{
    if (quality == "M") return {0, 4, 7};
    if (quality == "m") return {0, 3, 7};
    if (quality == "d") return {0, 3, 6};
    if (quality == "A") return {0, 4, 8};
    return {0, 4, 7};
}

// ── Constructor ────────────────────────────────────────────────────────────

HarmonyEngine::HarmonyEngine() { buildScale(); }

// ── Configuration ──────────────────────────────────────────────────────────

void HarmonyEngine::setMood (const juce::String& mood)
{
    if (findMood (mood) == nullptr) return;
    currentMood = mood;
    targetRegisterCenter = getMoodRegisterTarget (mood);
    buildScale();
}

void HarmonyEngine::setKey (int rootMidi)  { rootMidiNote = rootMidi; buildScale(); resetVoiceLeadingState(); }
void HarmonyEngine::setExtensionTier (int tier) { extensionTier = juce::jlimit (1, 4, tier); }
void HarmonyEngine::setColorAmount (float amount) { colorAmount = juce::jlimit (0.0f, 1.0f, amount); }
void HarmonyEngine::setVoicing (int v) { voicingSetting = juce::jlimit (-5, 5, v); }

// ── Build scale ────────────────────────────────────────────────────────────

void HarmonyEngine::buildScale()
{
    const MoodData* m = findMood (currentMood);
    if (m == nullptr) return;
    scaleIntervals.assign (m->intervals, m->intervals + 7);
    scaleQualities.clear();
    for (int i = 0; i < 7; ++i) scaleQualities.push_back (m->qualities[i]);
    scaleMidi.clear();
    for (int i = 0; i < 7; ++i) scaleMidi.push_back (rootMidiNote + scaleIntervals[(size_t) i]);
    for (int d = 0; d < 7; ++d) computeExtensions (d);
}

// ── Extensions ─────────────────────────────────────────────────────────────

void HarmonyEngine::computeExtensions (int degIdx)
{
    const int n = (int) scaleIntervals.size();
    const int root = scaleIntervals[(size_t) degIdx];
    auto tone = [&] (int skip, int lo, int hi) -> int {
        int idx = (degIdx + skip) % n;
        int raw = scaleIntervals[(size_t) idx] - root;
        while (raw < lo) raw += 12;
        while (raw > hi) raw -= 12;
        return raw;
    };
    int s7 = tone (6, 10, 11), s9 = tone (8, 13, 15), s13 = tone (12, 20, 21);
    if (currentMood == "Tender" && degIdx == 0 && s7 == 11) s7 = 10;
    auto& ext = extData[(size_t) degIdx];
    ext.clear();
    ext[1] = {};  ext[2] = {s7};  ext[3] = {s7, s9};  ext[4] = {s7, s9, s13};
}

// ── Color tier ─────────────────────────────────────────────────────────────

int HarmonyEngine::getColorTier (int degree, float ca)
{
    auto entry = getColorEntry (currentMood, degree);
    return (ca >= entry.threshold) ? entry.coloredTier : entry.baseTier;
}

// ── Voice (ascending, no gap > 19) ────────────────────────────────────────

std::vector<int> HarmonyEngine::voice (const std::vector<int>& notes)
{
    if (notes.empty()) return {};
    std::vector<int> out = { notes[0] };
    int prev = notes[0];
    for (size_t i = 1; i < notes.size(); ++i)
    {
        int n = notes[i];
        while (n <= prev)     n += 12;
        while (n - prev > 19) n -= 12;
        out.push_back (n);
        prev = n;
    }
    return out;
}

// ── Get chord ──────────────────────────────────────────────────────────────

std::vector<int> HarmonyEngine::getChord (int degree)
{
    if (degree < 1 || degree > 7) return {};

    // Dusk degree III: dual quality based on Color
    if (currentMood == "Dusk" && degree == 3)
    {
        int thirdRoot = scaleMidi[2];  // Eb in C Dorian
        std::vector<int> chord;
        if (colorAmount < 0.4f)
        {
            // IIImin7 — Eb minor quality (borrowed)
            chord.push_back (thirdRoot);
            chord.push_back (thirdRoot + 3);  // minor third (Gb)
            chord.push_back (thirdRoot + 7);  // fifth (Bb)
            if (colorAmount >= 0.2f)
                chord.push_back (thirdRoot + 10);  // b7 (Db)
        }
        else
        {
            // IIImaj7 — Eb major quality (diatonic)
            chord.push_back (thirdRoot);
            chord.push_back (thirdRoot + 4);  // major third (G)
            chord.push_back (thirdRoot + 7);  // fifth (Bb)
            if (colorAmount >= 0.6f)
                chord.push_back (thirdRoot + 11);  // maj7 (D)
        }
        auto voiced = voice (chord);
        if (voicingSetting != 0) voiced = applyVoicing (voiced, voicingSetting);
        return voiced;
    }

    int base = scaleMidi[(size_t) (degree - 1)];
    auto q = scaleQualities[(size_t) (degree - 1)];
    int tier = getColorTier (degree, colorAmount);
    auto triad = getTriad (q);
    auto& exts = extData[(size_t) (degree - 1)][tier];
    std::vector<int> notes;
    for (int iv : triad) notes.push_back (base + iv);
    for (int iv : exts)  notes.push_back (base + iv);
    auto voiced = voice (notes);
    if (voicingSetting != 0) voiced = applyVoicing (voiced, voicingSetting);
    return voiced;
}

// ── Get arp notes ──────────────────────────────────────────────────────────

std::vector<int> HarmonyEngine::getArpNotes (int degree)
{
    if (degree < 1 || degree > 7) return {};
    int base = scaleMidi[(size_t) (degree - 1)];
    auto q = scaleQualities[(size_t) (degree - 1)];
    int tier = getColorTier (degree, colorAmount);
    auto triad = getTriad (q);
    auto& exts = extData[(size_t) (degree - 1)][tier];
    std::vector<int> notes;
    for (int iv : triad) notes.push_back (base + iv);
    for (int iv : exts)  notes.push_back (base + iv);
    return notes;
}

// ── Get bass note ──────────────────────────────────────────────────────────

int HarmonyEngine::getBassNote (int degree, bool altBass)
{
    if (degree < 1 || degree > 7) return 0;
    int base = scaleMidi[(size_t) (degree - 1)] - 12;
    if (altBass)
    {
        auto triad = getTriad (scaleQualities[(size_t) (degree - 1)]);
        return base + triad[2];
    }
    return base;
}

// ── Mood register target ──────────────────────────────────────────────────

int HarmonyEngine::getMoodRegisterTarget (const juce::String& mood)
{
    if (mood == "Deep")    return 50;
    if (mood == "Hollow")  return 50;
    if (mood == "Tense")   return 52;
    if (mood == "Warm")    return 54;
    if (mood == "Bright")  return 55;
    if (mood == "Tender")  return 57;
    if (mood == "Dream")   return 59;
    if (mood == "Dusk")    return 53;
    return 55;
}

// ═══════════════════════════════════════════════════════════════════════════
// Voice leading — deterministic 4-voice assignment
// Voice 1: root (locked). Voices 2-4: optimal PC assignment.
// ═══════════════════════════════════════════════════════════════════════════

int HarmonyEngine::findNearestOctave (int pc, int target, int lo, int hi)
{
    int best = target, bestD = 999;
    for (int oct = 0; oct <= 9; ++oct)
    {
        int cand = oct * 12 + (pc % 12);
        if (cand < lo || cand > hi) continue;
        int d = std::abs (cand - target);
        if (d < bestD) { bestD = d; best = cand; }
    }
    return best;
}

std::vector<int> HarmonyEngine::getUpperPCs (const std::vector<int>& chordTones)
{
    if (chordTones.empty()) return {};
    int rootPC = chordTones[0] % 12;

    // Collect unique non-root pitch classes
    std::vector<int> pcs;
    for (int n : chordTones)
    {
        int pc = n % 12;
        if (pc != rootPC && std::find (pcs.begin(), pcs.end(), pc) == pcs.end())
            pcs.push_back (pc);
    }

    // Need exactly 3 upper PCs for voices 2-4
    if ((int) pcs.size() >= 4)
    {
        // 9th chord: third, fifth, seventh, ninth
        // At high color drop fifth (jazz voicing), else drop ninth
        if (colorAmount >= 0.5f)
            pcs.erase (pcs.begin() + 1);  // remove fifth
        else
            pcs.resize (3);  // keep third, fifth, seventh
    }

    // Pad if fewer than 3 (triad = 2 unique upper PCs)
    while ((int) pcs.size() < 3)
    {
        if (!pcs.empty())
            pcs.push_back (pcs.back());  // double last
        else
            pcs.push_back (rootPC);  // emergency
    }
    pcs.resize (3);
    return pcs;
}

std::vector<int> HarmonyEngine::getBestInversion (
    const std::vector<int>& chordTones,
    const std::vector<int>& prevVoices,
    float /*feel*/,
    int degree)
{
    if (chordTones.empty()) return chordTones;

    int rootPC = chordTones[0] % 12;

    // First chord or degree I: use canonical placement
    if (!voiceCountSet || prevVoices.empty() || degree == 1)
        return placeNearRegister (chordTones, targetRegisterCenter);

    // Voice 1: root — nearest octave to previous bass
    int prevBass = prevVoices.front();
    int bassNote = findNearestOctave (rootPC, prevBass, 24, 72);

    // Get 3 required upper pitch classes
    auto upperPCs = getUpperPCs (chordTones);

    // Previous upper voices (voices 2-4)
    std::vector<int> prevUpper;
    for (int i = 1; i < (int) prevVoices.size() && i <= 3; ++i)
        prevUpper.push_back (prevVoices[(size_t) i]);
    while ((int) prevUpper.size() < 3)
        prevUpper.push_back (prevUpper.empty() ? bassNote + 7 : prevUpper.back() + 4);
    prevUpper.resize (3);

    // Try all 6 permutations of 3 PCs → find minimum total movement
    int idx[3] = { 0, 1, 2 };
    int bestPerm[3] = { 0, 1, 2 };
    float bestCost = 999999.0f;

    do {
        float cost = 0.0f;
        int notes[3];
        for (int v = 0; v < 3; ++v)
        {
            int pc = upperPCs[(size_t) idx[v]];
            notes[v] = findNearestOctave (pc, prevUpper[(size_t) v], bassNote + 1, 84);
            cost += (float) std::abs (notes[v] - prevUpper[(size_t) v]);
        }
        // Penalize voice crossing
        for (int v = 0; v < 2; ++v)
            if (notes[v] >= notes[v + 1]) cost += 24.0f;

        // Register gravity — gentle bias when drifting
        float gravThreshold = 4.0f;
        if (std::abs (currentDrift) > gravThreshold)
        {
            float permMid = ((float) bassNote + (float) notes[0] + (float) notes[1] + (float) notes[2]) * 0.25f;
            float permDrift = permMid - (float) targetRegisterCenter;
            bool corrects = (currentDrift > 0 && permDrift < currentDrift)
                         || (currentDrift < 0 && permDrift > currentDrift);
            float gStr = juce::jmin (3.0f, (std::abs (currentDrift) - gravThreshold) * 0.25f);
            gStr *= (1.0f - feelAmount * 0.6f);  // weaker at high feel
            if (corrects) cost -= gStr;
            else          cost += gStr * 0.5f;
        }

        if (cost < (float) bestCost)
        {
            bestCost = cost;
            bestPerm[0] = idx[0]; bestPerm[1] = idx[1]; bestPerm[2] = idx[2];
        }
    } while (std::next_permutation (idx, idx + 3));

    // Place voices using best permutation
    std::vector<int> result;
    result.push_back (bassNote);

    for (int v = 0; v < 3; ++v)
    {
        int pc = upperPCs[(size_t) bestPerm[v]];
        int note = findNearestOctave (pc, prevUpper[(size_t) v], bassNote + 1, 84);
        result.push_back (note);
    }

    std::sort (result.begin(), result.end());

    // Verify root is present
    bool rootPresent = false;
    for (int n : result) if (n % 12 == rootPC) { rootPresent = true; break; }
    if (!rootPresent) result[0] = findNearestOctave (rootPC, result[0], 24, 72);

    std::sort (result.begin(), result.end());
    for (auto& n : result) n = juce::jlimit (21, 108, n);
    return result;
}

std::vector<int> HarmonyEngine::placeNearRegister (
    const std::vector<int>& chordTones, int targetCenter)
{
    if (chordTones.empty()) return chordTones;

    std::vector<int> pcs;
    for (int n : chordTones) pcs.push_back (n % 12);

    int rootPc = pcs[0];
    int bass = targetCenter - 6;
    while (bass % 12 != rootPc) bass--;
    if (bass < targetCenter - 12) bass += 12;
    if (bass > targetCenter - 1)  bass -= 12;

    std::vector<int> result;
    result.push_back (bass);

    // Get upper PCs
    auto upper = getUpperPCs (chordTones);
    int cur = bass;
    for (int pc : upper)
    {
        int note = cur + 1;
        while (note % 12 != pc) note++;
        result.push_back (note);
        cur = note;
    }

    for (auto& n : result) n = juce::jlimit (24, 96, n);
    std::sort (result.begin(), result.end());
    while ((int) result.size() > 4) result.pop_back();
    while ((int) result.size() < 4) result.push_back (result.back() + 12);

    voiceCountSet = true;
    return result;
}

void HarmonyEngine::updateDriftTracking (const std::vector<int>& voicedChord)
{
    if (voicedChord.empty()) return;
    float avg = 0.0f;
    for (int n : voicedChord) avg += (float) n;
    avg /= (float) voicedChord.size();

    recentMidpoints.push_back (avg);
    while ((int) recentMidpoints.size() > 6)
        recentMidpoints.pop_front();

    if ((int) recentMidpoints.size() >= 3)
    {
        float recentAvg = 0.0f;
        for (float m : recentMidpoints) recentAvg += m;
        recentAvg /= (float) recentMidpoints.size();
        currentDrift = recentAvg - (float) targetRegisterCenter;
    }
    else
        currentDrift = 0.0f;
}

void HarmonyEngine::resetVoiceLeadingState()
{
    voiceCountSet = false;
    recentMidpoints.clear();
    currentDrift = 0.0f;
    targetRegisterCenter = getMoodRegisterTarget (currentMood);
}

std::vector<int> HarmonyEngine::clampToRange (std::vector<int> notes)
{
    for (auto& n : notes)
    {
        while (n < 24) n += 12;
        while (n > 96) n -= 12;
    }
    std::sort (notes.begin(), notes.end());
    return notes;
}

// ═══════════════════════════════════════════════════════════════════════════
// VOICE LEADING — Melodic nearest-note system
//
// Each note is a voice. Between chords, each voice moves to the
// nearest available note in the next chord. Common tones stay.
// ═══════════════════════════════════════════════════════════════════════════

std::vector<int> HarmonyEngine::applyVoicing (
    const std::vector<int>& notes, int v)
{
    if (notes.size() < 2 || v == 0) return notes;
    std::vector<int> result = notes;
    int bass = result[0];
    if (v < 0)
    {
        int intensity = std::abs (v);
        for (int pass = 0; pass < intensity; ++pass)
        {
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (int i = (int) result.size() - 1; i > 0; --i)
                    if (result[(size_t) i] - result[(size_t)(i - 1)] > 11)
                    { result[(size_t) i] -= 12; changed = true; }
                std::vector<int> upper (result.begin() + 1, result.end());
                std::sort (upper.begin(), upper.end());
                result.resize (1); result[0] = bass;
                result.insert (result.end(), upper.begin(), upper.end());
            }
            if (pass > 0)
            {
                for (int i = (int) result.size() - 1; i > 0; --i)
                    if (result[(size_t) i] - result[(size_t)(i - 1)] > 7)
                        result[(size_t) i] -= 12;
                std::vector<int> upper (result.begin() + 1, result.end());
                std::sort (upper.begin(), upper.end());
                result.resize (1); result[0] = bass;
                result.insert (result.end(), upper.begin(), upper.end());
            }
        }
    }
    else
    {
        std::vector<int> upper (result.begin() + 1, result.end());
        auto sz = [&]() { return (int) upper.size(); };
        if (v >= 1 && sz() >= 1) upper[(size_t)(sz() - 1)] += 12;
        if (v >= 2 && sz() >= 2) upper[(size_t)(sz() - 2)] += 12;
        if (v >= 3 && sz() >= 2) upper[(size_t)(sz() - 1)] += 12;
        if (v >= 4 && sz() >= 3) upper[(size_t)(sz() - 3)] += 12;
        if (v >= 5 && sz() >= 2) upper[(size_t)(sz() - 1)] += 12;
        std::sort (upper.begin(), upper.end());
        result.clear(); result.push_back (bass);
        result.insert (result.end(), upper.begin(), upper.end());
    }
    return result;
}

// ── Chord naming ───────────────────────────────────────────────────────────

juce::String HarmonyEngine::buildSuffix (const juce::String& quality,
                                          const std::vector<int>& exts)
{
    if (exts.empty())
    {
        if (quality == "M") return "";
        if (quality == "m") return "m";
        if (quality == "d") return "dim";
        if (quality == "A") return "aug";
        return "";
    }
    int s7 = exts[0];
    bool has9 = exts.size() >= 2;
    bool h13 = exts.size() >= 3;
    if (quality == "M")
    {
        juce::String b = (s7 >= 11) ? "maj7" : "7";
        if (has9) b = b.replace ("7", "9");
        if (h13)  b = b.replace ("9", "13");
        return b;
    }
    if (quality == "m")
    {
        if (h13)  return "m13";
        if (has9) return "m9";
        return "m7";
    }
    if (quality == "d")
        return (s7 <= 11) ? (juce::String::fromUTF8 ("\xc3\xb8") + "7") : "dim";
    if (quality == "A")
    {
        juce::String b = (s7 >= 11) ? "augM7" : "aug7";
        if (has9) b = b.replace ("7", "9");
        if (h13)  b = b.replace ("9", "13");
        return b;
    }
    return "";
}

juce::String HarmonyEngine::getChordName (int degree)
{
    if (degree < 1 || degree > 7) return "?";
    int rootPc = rootMidiNote % 12;
    bool useFlats = (rootPc == 5 || rootPc == 10 || rootPc == 3 ||
                     rootPc == 8 || rootPc == 1  || rootPc == 6);
    const auto* names = useFlats ? notesFlat : notesSharp;
    int rootIdx = (rootMidiNote + scaleIntervals[(size_t) (degree - 1)]) % 12;
    auto q = scaleQualities[(size_t) (degree - 1)];
    int tier = getColorTier (degree, colorAmount);
    auto& exts = extData[(size_t) (degree - 1)][tier];
    auto suffix = buildSuffix (q, exts);
    static const char* romanUp[] = {"I","II","III","IV","V","VI","VII"};
    static const char* romanLo[] = {"i","ii","iii","iv","v","vi","vii"};
    juce::String roman;
    if (q == "M")      roman = romanUp[degree - 1];
    else if (q == "A") roman = juce::String (romanUp[degree - 1]) + "+";
    else if (q == "d") roman = juce::String (romanLo[degree - 1]) + juce::String::fromUTF8 ("\xc2\xb0");
    else               roman = romanLo[degree - 1];
    return names[rootIdx] + suffix + "  (" + roman + ")";
}

juce::String HarmonyEngine::getChordShort (int degree)
{ return getChordName (degree).upToFirstOccurrenceOf ("  ", false, false); }

juce::String HarmonyEngine::getMoodLabel() { return currentMood; }

juce::String HarmonyEngine::getChordQuality (int degree)
{
    if (degree < 1 || degree > 7) return "M";
    return scaleQualities[(size_t) (degree - 1)];
}
