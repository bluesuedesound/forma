#include "HarmonyEngine.h"

// ── Static note name arrays ────────────────────────────────────────────────

const juce::String HarmonyEngine::notesSharp[12] =
    { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

const juce::String HarmonyEngine::notesFlat[12] =
    { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };

// ── Mood names ─────────────────────────────────────────────────────────────

const juce::StringArray HarmonyEngine::moodNames =
    { "Bright", "Warm", "Dream", "Deep", "Hollow", "Tender", "Tense", "Dusk",
      "Crest", "Nocturne", "Shimmer", "Static" };

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
    // ── Bright Lights pack ──
    { "Crest",    {0,2,4,5,7,9,11},  {"M","m","m","M","M","m","d"} },
    { "Nocturne", {0,2,3,5,7,8,10},  {"m","d","M","m","m","M","M"} },
    { "Shimmer",  {0,2,4,5,7,9,10},  {"M","m","d","M","m","m","M"} },
    { "Static",   {0,2,4,5,7,9,10},  {"M","M","m","M","M","m","M"} },
};
static constexpr int numMoods = 12;

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

    else if (mood == "Crest")
    {
        switch (degree) {
            case 1: return {1, 1, 1.1f};   // stays triad
            case 2: return {1, 2, 0.35f};  // min7 at mid Color
            case 3: return {1, 2, 0.55f};  // min7 at mid-high
            case 4: return {1, 2, 0.45f};  // add9 at mid, maj7 at high
            case 5: return {1, 2, 0.30f};  // dom7 early
            case 6: return {1, 2, 0.60f};  // min7 at high Color
            case 7: return {1, 2, 0.80f};  // half-dim7 late
        }
    }
    else if (mood == "Nocturne")
    {
        switch (degree) {
            case 1: return {1, 2, 0.70f};  // minor, stays pure low
            case 2: return {1, 2, 0.50f};  // half-dim → min7 at mid
            case 3: return {1, 2, 0.55f};  // major, maj7 at mid
            case 4: return {1, 2, 0.70f};  // minor, becomes major at high
            case 5: return {1, 2, 0.40f};  // minor → dom7 at mid
            case 6: return {1, 2, 0.55f};  // major, maj7
            case 7: return {1, 2, 0.60f};  // major, dom7
        }
    }
    else if (mood == "Shimmer")
    {
        switch (degree) {
            case 1: return {1, 2, 0.25f};  case 2: return {1, 2, 0.55f};
            case 3: return {1, 1, 1.1f};   case 4: return {1, 2, 0.45f};
            case 5: return {1, 1, 1.1f};   case 6: return {1, 2, 0.50f};
            case 7: return {1, 2, 0.60f};
        }
    }
    else if (mood == "Static")
    {
        switch (degree) {
            case 1: return {1, 2, 0.50f};  case 2: return {1, 2, 0.50f};
            case 3: return {1, 2, 0.55f};  case 4: return {1, 2, 0.45f};
            case 5: return {1, 2, 0.30f};  case 6: return {1, 2, 0.55f};
            case 7: return {1, 2, 0.60f};
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
    if (mood != currentMood) clearVoicingCache();
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

// ── Chord tone by index with fallback ──────────────────────────────────────

int HarmonyEngine::getChordToneInterval (int degree, int toneIdx, int* outResolvedIdx)
{
    if (degree < 1 || degree > 7)
    {
        if (outResolvedIdx) *outResolvedIdx = 0;
        return 0;
    }

    auto q = scaleQualities[(size_t) (degree - 1)];
    auto triad = getTriad (q);  // { 0, 3rd, 5th }
    int tier = getColorTier (degree, colorAmount);
    auto& exts = extData[(size_t) (degree - 1)][tier];

    // Extensions in computeExtensions are ordered { s7, s9, s13 } per tier.
    // Index into exts: tier=2 → [s7]; tier=3 → [s7, s9]; tier=4 → [s7, s9, s13].
    auto extAt = [&] (int which) -> int {
        // which: 0=7, 1=9, 2=13
        if (which < (int) exts.size()) return exts[(size_t) which];
        return INT_MIN;  // not present
    };

    auto tryTone = [&] (int idx) -> int {
        switch (idx)
        {
            case 0: return 0;             // root
            case 1: return triad[1];      // 3rd
            case 2: return triad[2];      // 5th
            case 3: return extAt (0);     // 7th
            case 4: return extAt (1);     // 9th
            case 5: return 5;             // 11th — synthesised as perfect 4th
            case 6: return extAt (2);     // 13th
            default: return INT_MIN;
        }
    };

    // Fallback order per spec:
    //   7 → 5 → 3 → 1
    //   9 → 7 → 5 → 3 → 1
    //   11 → 9 → 7 → 5 → 3 → 1
    //   13 → 11 → 9 → 7 → 5 → 3 → 1
    static const std::vector<std::vector<int>> chains = {
        { 0 },                           // 1
        { 1, 0 },                        // 3
        { 2, 1, 0 },                     // 5
        { 3, 2, 1, 0 },                  // 7
        { 4, 3, 2, 1, 0 },               // 9
        { 5, 4, 3, 2, 1, 0 },            // 11 (11 always available since synthesised)
        { 6, 5, 4, 3, 2, 1, 0 }          // 13
    };

    int clampedIdx = juce::jlimit (0, 6, toneIdx);
    for (int candidate : chains[(size_t) clampedIdx])
    {
        int v = tryTone (candidate);
        if (v != INT_MIN)
        {
            if (outResolvedIdx) *outResolvedIdx = candidate;
            return v;
        }
    }
    if (outResolvedIdx) *outResolvedIdx = 0;
    return 0;
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
    if (mood == "Dusk")     return 53;
    if (mood == "Crest")    return 55;
    if (mood == "Nocturne") return 50;
    if (mood == "Shimmer")  return 54;
    if (mood == "Static")   return 55;
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

// Octave selector with two modes:
//
//   No cache (cacheHint < 0 or cacheWeight <= 0): drift-aware tiebreaker
//   from Pass 1 / F+C+D. Among candidates within slack of the strict-nearest
//   distance, pick the octave whose direction best opposes currentDrift.
//
//   With cache (Pass 2): the candidate set is the octaves of pc within
//   ±12 of target and legal range, plus the cached octave when it lies
//   within an `effectiveCap` window around target. Each candidate is
//   scored by `smoothness + pull`, where pull = |c - cacheHint| · weight ·
//   cachePullWeight. The cap depends on weight (recency strength) and
//   color (jazz-ness): 12 / 7 / 4 for weight ≥ 0.5 / ≥ 0.2 / else,
//   attenuated by color, floor 4. The cap controls reachability of the
//   cached octave when it's outside the smoothness ±12 window — within
//   ±12 it's a candidate regardless and the pull does the work.
int HarmonyEngine::findNearestOctaveDriftAware (int pc, int target, int lo, int hi,
                                                  int cacheHint, float cacheWeight)
{
    // ── No cache: Pass 1 drift-aware tiebreaker. ──────────────────────────
    if (cacheHint < 0 || cacheWeight <= 0.0f)
    {
        int bestDist = INT_MAX;
        for (int oct = 0; oct <= 9; ++oct)
        {
            int cand = oct * 12 + (pc % 12);
            if (cand < lo || cand > hi) continue;
            int d = std::abs (cand - target);
            if (d < bestDist) bestDist = d;
        }
        if (bestDist == INT_MAX) return target;

        const float absDrift = std::abs (currentDrift);
        const float slack = (absDrift < 4.0f)
                                ? 0.0f
                                : juce::jmin (8.0f, 0.25f * absDrift);

        int best = -1;
        int bestScore = INT_MAX;
        for (int oct = 0; oct <= 9; ++oct)
        {
            int cand = oct * 12 + (pc % 12);
            if (cand < lo || cand > hi) continue;
            int d = std::abs (cand - target);
            if ((float) (d - bestDist) > slack) continue;

            int score;
            if      (currentDrift > 0.0f) score =  cand;
            else if (currentDrift < 0.0f) score = -cand;
            else                          score =  d;
            if (score < bestScore) { bestScore = score; best = cand; }
        }
        return (best == -1) ? target : best;
    }

    // ── Cache-aware: Pass 2 cap + pull. ───────────────────────────────────
    float baseCap = (cacheWeight >= 0.5f) ? 12.0f
                  : (cacheWeight >= 0.2f) ?  7.0f
                                          :  4.0f;
    float effectiveCap = baseCap * (1.0f - colorAmount * 0.5f);
    if (effectiveCap < 4.0f) effectiveCap = 4.0f;
    const int capInt = (int) std::round (effectiveCap);

    // Candidates: octaves of pc within ±12 of target and legal range.
    int cands[16];
    int nCands = 0;
    for (int oct = 0; oct <= 9; ++oct)
    {
        int c = oct * 12 + (pc % 12);
        if (c < lo || c > hi) continue;
        if (std::abs (c - target) > 12) continue;
        if (nCands < 16) cands[nCands++] = c;
    }

    // Cache override: ensure cached octave is in the candidate list when
    // it's reachable within the cap (may lie outside the ±12 smoothness
    // window when cap > 12 — currently capped at 12, so this only adds
    // a candidate that was already included; kept for forward-compat).
    if (cacheHint >= lo && cacheHint <= hi
        && std::abs (cacheHint - target) <= capInt)
    {
        bool present = false;
        for (int i = 0; i < nCands; ++i)
            if (cands[i] == cacheHint) { present = true; break; }
        if (!present && nCands < 16) cands[nCands++] = cacheHint;
    }

    if (nCands == 0)
        return findNearestOctave (pc, target, lo, hi);

    // Score each candidate: smoothness (distance from prev) + cache pull
    // (penalty for distance from cached, scaled by recency weight).
    constexpr float cachePullWeight = 0.8f;
    int   best = cands[0];
    float bestCost = std::numeric_limits<float>::infinity();
    for (int i = 0; i < nCands; ++i)
    {
        int c = cands[i];
        float smooth = (float) std::abs (c - target);
        float pull   = (float) std::abs (c - cacheHint) * cacheWeight * cachePullWeight;
        float total  = smooth + pull;
        if (total < bestCost) { bestCost = total; best = c; }
    }
    return best;
}

// ── Voicing cache ──────────────────────────────────────────────────────────

void HarmonyEngine::clearVoicingCache()
{
    for (auto& e : voicingCache) e.functionId = -1;
    cacheNextSlot = 0;
    cacheSize = 0;
    currentFunctionId = -1;
}

void HarmonyEngine::ageVoicingCache()
{
    for (int i = 0; i < cacheSize; ++i)
        voicingCache[(size_t) i].ageInChords += 1.0f;
}

void HarmonyEngine::commitVoicingToCache (int degree, const std::vector<int>& v)
{
    if (degree < 1 || degree > 7) return;
    if (v.size() < 4) return;

    auto& slot = voicingCache[(size_t) cacheNextSlot];
    slot.functionId  = degree;
    slot.voicing[0]  = v[0];
    slot.voicing[1]  = v[1];
    slot.voicing[2]  = v[2];
    slot.voicing[3]  = v[3];
    slot.colorTier   = getColorTier (degree, colorAmount);
    slot.ageInChords = 0.0f;

    cacheNextSlot = (cacheNextSlot + 1) % kCacheCapacity;
    if (cacheSize < kCacheCapacity) ++cacheSize;

   #if JUCE_DEBUG
    DBG ("voicingCache write: deg=" + juce::String (degree)
         + " slot=" + juce::String ((cacheNextSlot + kCacheCapacity - 1) % kCacheCapacity)
         + " v=[" + juce::String (v[0]) + " " + juce::String (v[1]) + " "
                  + juce::String (v[2]) + " " + juce::String (v[3]) + "]"
         + " size=" + juce::String (cacheSize));
   #endif
}

float HarmonyEngine::computeAttractionDelta (const int candidateNotes[4]) const
{
    if (currentFunctionId < 0 || cacheSize == 0) return 0.0f;

    const float decayRate = 0.05f + colorAmount * 0.45f;
    constexpr float cacheAttractionWeight = 0.6f;

    float sum = 0.0f;
    for (int i = 0; i < cacheSize; ++i)
    {
        const auto& e = voicingCache[(size_t) i];
        if (e.functionId != currentFunctionId) continue;
        const float weight = std::exp (-e.ageInChords * decayRate);
        float voiceDist = 0.0f;
        for (int v = 0; v < 4; ++v)
            voiceDist += (float) std::abs (candidateNotes[v] - e.voicing[v]);
        sum += weight * voiceDist;
    }
    return cacheAttractionWeight * sum;
}

int HarmonyEngine::findRecentCachedBass (int functionId) const
{
    auto* e = findRecentCachedEntry (functionId);
    return e ? e->voicing[0] : -1;
}

const HarmonyEngine::CachedVoicing*
HarmonyEngine::findRecentCachedEntry (int functionId) const
{
    if (functionId < 0 || cacheSize == 0) return nullptr;
    int   bestIdx = -1;
    float bestAge = 1e9f;
    for (int i = 0; i < cacheSize; ++i)
    {
        const auto& e = voicingCache[(size_t) i];
        if (e.functionId != functionId) continue;
        if (e.ageInChords < bestAge) { bestAge = e.ageInChords; bestIdx = i; }
    }
    return (bestIdx == -1) ? nullptr : &voicingCache[(size_t) bestIdx];
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

    // Cache lifecycle: each chord-press ages all entries by 1, and the
    // function being voiced is exposed so the cost loop & bass octave
    // selection can query the cache for matching entries.
    currentFunctionId = (degree >= 1 && degree <= 7) ? degree : -1;
    ageVoicingCache();

   #if JUCE_DEBUG
    {
        int matches = 0;
        for (int i = 0; i < cacheSize; ++i)
            if (voicingCache[(size_t) i].functionId == currentFunctionId) ++matches;
        DBG ("voicingCache read: deg=" + juce::String (degree)
             + " size=" + juce::String (cacheSize)
             + " matches=" + juce::String (matches)
             + " decay=" + juce::String (0.05f + colorAmount * 0.45f, 3));
    }
   #endif

    // First-chord fallback only. We no longer snap to canonical on the I
    // chord — returning to tonic should voice-lead smoothly from whatever
    // the predecessor was, not reset to a fixed stack.
    if (!voiceCountSet || prevVoices.empty())
        return placeNearRegister (chordTones, targetRegisterCenter);

    // Pass 2: per-voice cache hints. The most-recent cached entry for the
    // current function provides 4 sorted MIDI targets — one per voice.
    // Both prev and cached voicings are stored sorted, so voice index v
    // in current corresponds to voice v in cached.
    const CachedVoicing* cached = findRecentCachedEntry (currentFunctionId);
    const float decayRate = 0.05f + colorAmount * 0.45f;
    const float cacheWeight = cached ? std::exp (-cached->ageInChords * decayRate) : 0.0f;
    const int cachedBass = cached ? cached->voicing[0] : -1;

    // Voice 1: root — cache-aware octave selection (cap + pull when
    // cached entry exists, drift-aware tiebreaker otherwise).
    int prevBass = prevVoices.front();
    int bassNote = findNearestOctaveDriftAware (rootPC, prevBass, 24, 72, cachedBass, cacheWeight);

    // Get 3 required upper pitch classes
    auto upperPCs = getUpperPCs (chordTones);

    // Previous upper voices (voices 2-4)
    std::vector<int> prevUpper;
    for (int i = 1; i < (int) prevVoices.size() && i <= 3; ++i)
        prevUpper.push_back (prevVoices[(size_t) i]);
    while ((int) prevUpper.size() < 3)
        prevUpper.push_back (prevUpper.empty() ? bassNote + 7 : prevUpper.back() + 4);
    prevUpper.resize (3);

    // Per-voice upper cache hints (cached.voicing[1..3]) or -1 each.
    int upperHint[3] = { -1, -1, -1 };
    if (cached)
        for (int v = 0; v < 3; ++v) upperHint[v] = cached->voicing[v + 1];

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
            notes[v] = findNearestOctaveDriftAware (pc, prevUpper[(size_t) v],
                                                     bassNote + 1, 84,
                                                     upperHint[v], cacheWeight);
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

        // Cache attraction — pulls candidate toward voicings of the same
        // function from earlier in the progression, weighted by recency
        // (decay rate driven by color knob). Compares the sorted 4-voice
        // candidate against the sorted cached entries voice-by-voice.
        {
            int cand[4] = { bassNote, notes[0], notes[1], notes[2] };
            std::sort (cand, cand + 4);
            cost += computeAttractionDelta (cand);
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
        int note = findNearestOctaveDriftAware (pc, prevUpper[(size_t) v],
                                                 bassNote + 1, 84,
                                                 upperHint[v], cacheWeight);
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
    clearVoicingCache();
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
