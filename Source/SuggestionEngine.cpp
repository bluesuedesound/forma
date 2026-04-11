#include "SuggestionEngine.h"
#include "BinaryData.h"

void SuggestionEngine::loadModel (const juce::var& modelData)
{
    firstOrder.clear();
    secondOrder.clear();

    auto* root = modelData.getDynamicObject();
    if (root == nullptr) return;

    auto moodsVar = root->getProperty (juce::Identifier ("moods"));
    auto* moodsObj = moodsVar.getDynamicObject();
    if (moodsObj == nullptr) return;

    for (auto& moodEntry : moodsObj->getProperties())
    {
        std::string mood = moodEntry.name.toString().toStdString();
        auto* moodData = moodEntry.value.getDynamicObject();
        if (moodData == nullptr) continue;

        auto foVar = moodData->getProperty (juce::Identifier ("first_order"));
        if (auto* foObj = foVar.getDynamicObject())
        {
            for (auto& fromEntry : foObj->getProperties())
            {
                int fromDeg = fromEntry.name.toString().getIntValue();
                if (auto* toObj = fromEntry.value.getDynamicObject())
                    for (auto& toEntry : toObj->getProperties())
                        firstOrder[mood][fromDeg][toEntry.name.toString().getIntValue()]
                            = (float)(double) toEntry.value;
            }
        }

        auto soVar = moodData->getProperty (juce::Identifier ("second_order"));
        if (auto* soObj = soVar.getDynamicObject())
        {
            for (auto& pairEntry : soObj->getProperties())
            {
                std::string pairKey = pairEntry.name.toString().toStdString();
                if (auto* toObj = pairEntry.value.getDynamicObject())
                    for (auto& toEntry : toObj->getProperties())
                        secondOrder[mood][pairKey][toEntry.name.toString().getIntValue()]
                            = (float)(double) toEntry.value;
            }
        }
    }

    for (auto& mood : { "Bright", "Warm", "Dream", "Deep", "Hollow", "Tender", "Tense" })
    {
        std::string m (mood);
        DBG ("Forma suggestion model: " + juce::String (mood)
             + " fo=" + juce::String ((int) firstOrder[m].size())
             + " so=" + juce::String ((int) secondOrder[m].size()));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Zone system — Roman numeral to integer degree mapping
// ═════════════════════════════════════════════════════════════════════════

int SuggestionEngine::romanToInt (const juce::String& roman)
{
    if (roman == "I"    || roman == "i")                        return 1;
    if (roman == "II"   || roman == "ii")                       return 2;
    if (roman == "III"  || roman == "iii"  || roman == "bIII")  return 3;
    if (roman == "IV"   || roman == "iv")                       return 4;
    if (roman == "V"    || roman == "v")                        return 5;
    if (roman == "VI"   || roman == "vi"   || roman == "bVI")   return 6;
    if (roman == "VII"  || roman == "vii"  || roman == "bVII")  return 7;
    return -1;
}

// ═════════════════════════════════════════════════════════════════════════
// Zone key selection — maps mood index + color amount to a zone key
// ═════════════════════════════════════════════════════════════════════════

juce::String SuggestionEngine::getZoneKey (int moodIndex, float colorAmount) const
{
    static const char* MOOD_NAMES[] = {
        "bright", "warm", "dream", "deep",
        "hollow", "tender", "tense", "dusk",
        "crest", "nocturne", "shimmer", "static"
    };

    if (moodIndex < 0 || moodIndex > 11)
        return "bright_zone1";

    juce::String moodName = MOOD_NAMES[moodIndex];
    int maxZones = 2;
    auto it = moodZoneCounts.find (moodName);
    if (it != moodZoneCounts.end())
        maxZones = it->second;

    int zoneNum = 1;
    if (maxZones == 2)
    {
        zoneNum = colorAmount < 0.5f ? 1 : 2;
    }
    else if (maxZones == 3)
    {
        if      (colorAmount < 0.33f) zoneNum = 1;
        else if (colorAmount < 0.67f) zoneNum = 2;
        else                          zoneNum = 3;
    }
    else if (maxZones >= 4)
    {
        if      (colorAmount < 0.25f) zoneNum = 1;
        else if (colorAmount < 0.50f) zoneNum = 2;
        else if (colorAmount < 0.75f) zoneNum = 3;
        else                          zoneNum = 4;
    }

    return moodName + "_zone" + juce::String (zoneNum);
}

// ═════════════════════════════════════════════════════════════════════════
// Load all zones from BinaryData at startup
// ═════════════════════════════════════════════════════════════════════════

void SuggestionEngine::loadZonesFromBinaryData()
{
    // Step 1: Load zones index
    int indexSize = 0;
    auto* indexData = BinaryData::getNamedResource ("zones_index_json", indexSize);
    if (indexData == nullptr || indexSize == 0)
    {
        DBG ("zones_index.json not found in BinaryData — zones disabled");
        return;
    }

    auto indexJson = juce::JSON::parse (juce::String::fromUTF8 (indexData, indexSize));
    if (indexJson.isVoid())
    {
        DBG ("zones_index.json parse failed");
        return;
    }

    // Build mood zone counts from the index
    moodZoneCounts.clear();
    auto* indexObj = indexJson.getDynamicObject();
    if (indexObj == nullptr) return;

    juce::StringArray zoneKeys;

    for (auto& prop : indexObj->getProperties())
    {
        juce::String key = prop.name.toString();
        int zoneIdx = key.indexOf ("_zone");
        if (zoneIdx < 0) continue;

        juce::String moodName = key.substring (0, zoneIdx).toLowerCase();
        int zoneNum = key.substring (zoneIdx + 5).getIntValue();

        auto& count = moodZoneCounts[moodName];
        if (zoneNum > count)
            count = zoneNum;

        zoneKeys.add (key);
    }

    DBG ("Zone index loaded. Moods with zones:");
    for (auto& kv : moodZoneCounts)
        DBG ("  " + kv.first + ": " + juce::String (kv.second) + " zones");

    // Step 2: Load each zone's transitions
    for (auto& zoneKey : zoneKeys)
    {
        // BinaryData name: "bright_zone1_json" from "bright_zone1.json"
        juce::String binaryName = zoneKey + "_json";
        int dataSize = 0;
        auto* data = BinaryData::getNamedResource (binaryName.toRawUTF8(), dataSize);
        if (data == nullptr || dataSize == 0)
        {
            DBG ("Zone binary not found: " + binaryName);
            continue;
        }

        auto zoneJson = juce::JSON::parse (juce::String::fromUTF8 (data, dataSize));
        if (zoneJson.isVoid() || ! zoneJson.hasProperty ("transitions"))
        {
            DBG ("Zone parse failed: " + zoneKey);
            continue;
        }

        // Parse transitions: Roman numerals → integer degrees
        auto transVar = zoneJson.getProperty ("transitions", {});
        auto* transObj = transVar.getDynamicObject();
        if (transObj == nullptr) continue;

        FirstOrderModel model;

        // Accumulate raw probabilities (may merge I/i → 1, V/v → 5, etc.)
        // rawSums[fromDeg] tracks the total weight for renormalization
        std::map<int, float> rawSums;

        for (auto& fromEntry : transObj->getProperties())
        {
            int fromDeg = romanToInt (fromEntry.name.toString());
            if (fromDeg < 0) continue;

            auto* toObj = fromEntry.value.getDynamicObject();
            if (toObj == nullptr) continue;

            for (auto& toEntry : toObj->getProperties())
            {
                int toDeg = romanToInt (toEntry.name.toString());
                if (toDeg < 0) continue;

                float prob = (float)(double) toEntry.value;
                model[fromDeg][toDeg] += prob;
                rawSums[fromDeg] += prob;
            }
        }

        // Renormalize each from-degree row (handles merged variants)
        for (auto& fromEntry : model)
        {
            float total = rawSums[fromEntry.first];
            if (total > 0.0f && std::abs (total - 1.0f) > 0.001f)
            {
                for (auto& toEntry : fromEntry.second)
                    toEntry.second /= total;
            }
        }

        zoneModels[zoneKey] = std::move (model);
        DBG ("Zone preloaded: " + zoneKey
             + " (" + juce::String ((int) zoneModels[zoneKey].size()) + " from-degrees)");
    }

    // Step 3: Load pack zones (Bright Lights)
    {
        struct PackZone { const char* binaryName; const char* zoneKey; const char* mood; int zoneNum; };
        static const PackZone packZones[] = {
            { "packs_bright_lights_crest_zone1_json",    "crest_zone1",    "crest",    1 },
            { "packs_bright_lights_crest_zone2_json",    "crest_zone2",    "crest",    2 },
            { "packs_bright_lights_crest_zone3_json",    "crest_zone3",    "crest",    3 },
            { "packs_bright_lights_crest_zone4_json",    "crest_zone4",    "crest",    4 },
            { "packs_bright_lights_nocturne_zone1_json", "nocturne_zone1", "nocturne", 1 },
            { "packs_bright_lights_nocturne_zone2_json", "nocturne_zone2", "nocturne", 2 },
            { "packs_bright_lights_nocturne_zone3_json", "nocturne_zone3", "nocturne", 3 },
            { "packs_bright_lights_shimmer_zone1_json",  "shimmer_zone1",  "shimmer",  1 },
            { "packs_bright_lights_shimmer_zone2_json",  "shimmer_zone2",  "shimmer",  2 },
            { "packs_bright_lights_shimmer_zone3_json",  "shimmer_zone3",  "shimmer",  3 },
            { "packs_bright_lights_static_zone1_json",   "static_zone1",   "static",   1 },
            { "packs_bright_lights_static_zone2_json",   "static_zone2",   "static",   2 },
            { "packs_bright_lights_static_zone3_json",   "static_zone3",   "static",   3 },
        };

        for (auto& pz : packZones)
        {
            int dataSize = 0;
            auto* data = BinaryData::getNamedResource (pz.binaryName, dataSize);
            if (data == nullptr || dataSize == 0)
            {
                DBG ("Pack zone binary not found: " + juce::String (pz.binaryName));
                continue;
            }

            auto zoneJson = juce::JSON::parse (juce::String::fromUTF8 (data, dataSize));
            if (zoneJson.isVoid() || ! zoneJson.hasProperty ("transitions"))
            {
                DBG ("Pack zone parse failed: " + juce::String (pz.zoneKey));
                continue;
            }

            auto transVar = zoneJson.getProperty ("transitions", {});
            auto* transObj = transVar.getDynamicObject();
            if (transObj == nullptr) continue;

            FirstOrderModel model;
            std::map<int, float> rawSums;

            for (auto& fromEntry : transObj->getProperties())
            {
                int fromDeg = romanToInt (fromEntry.name.toString());
                if (fromDeg < 0) continue;

                auto* toObj = fromEntry.value.getDynamicObject();
                if (toObj == nullptr) continue;

                for (auto& toEntry : toObj->getProperties())
                {
                    int toDeg = romanToInt (toEntry.name.toString());
                    if (toDeg < 0) continue;

                    float prob = (float)(double) toEntry.value;
                    model[fromDeg][toDeg] += prob;
                    rawSums[fromDeg] += prob;
                }
            }

            for (auto& fromEntry : model)
            {
                float total = rawSums[fromEntry.first];
                if (total > 0.0f && std::abs (total - 1.0f) > 0.001f)
                    for (auto& toEntry : fromEntry.second)
                        toEntry.second /= total;
            }

            juce::String key (pz.zoneKey);
            zoneModels[key] = std::move (model);
            DBG ("Pack zone preloaded: " + key
                 + " (" + juce::String ((int) zoneModels[key].size()) + " from-degrees)");

            // Update mood zone counts
            juce::String moodName (pz.mood);
            auto& count = moodZoneCounts[moodName];
            if (pz.zoneNum > count)
                count = pz.zoneNum;
        }

        DBG ("Pack zones loaded. Updated mood zone counts:");
        for (auto& kv : moodZoneCounts)
            DBG ("  " + kv.first + ": " + juce::String (kv.second) + " zones");
    }

    zonesLoaded = true;
    DBG ("All zones preloaded: " + juce::String ((int) zoneModels.size()) + " zone models");

    // Load default zone: Bright mood (index 0), mid Color
    updateZone (0, 0.5f);
}

// ═════════════════════════════════════════════════════════════════════════
// Update active zone — safe to call from any thread (atomic pointer swap)
// ═════════════════════════════════════════════════════════════════════════

void SuggestionEngine::updateZone (int moodIndex, float colorAmount)
{
    if (! zonesLoaded) return;

    juce::String newKey = getZoneKey (moodIndex, colorAmount);
    if (newKey == currentZoneKey) return;

    auto it = zoneModels.find (newKey);
    if (it == zoneModels.end())
    {
        DBG ("Zone not found in preloaded models: " + newKey + " — keeping current");
        return;
    }

    activeZoneModel.store (&it->second, std::memory_order_release);
    currentZoneKey = newKey;
    DBG ("Zone active: " + newKey);
}

// ═════════════════════════════════════════════════════════════════════════
// Markov transition probability (zone → second-order → first-order → uniform)
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::getTransitionProb (const juce::String& mood,
                                            int prevDegree, int fromDegree, int toDegree)
{
    std::string moodStr = mood.toStdString();

    // Second-order: use original model (zones are first-order only)
    if (prevDegree >= 1)
    {
        std::string pairKey = std::to_string (prevDegree) + "," + std::to_string (fromDegree);
        auto moodIt = secondOrder.find (moodStr);
        if (moodIt != secondOrder.end())
        {
            auto pairIt = moodIt->second.find (pairKey);
            if (pairIt != moodIt->second.end())
            {
                auto degIt = pairIt->second.find (toDegree);
                if (degIt != pairIt->second.end())
                    return degIt->second;
            }
        }
    }

    // First-order: prefer active zone model over original
    auto* zone = activeZoneModel.load (std::memory_order_acquire);
    if (zone != nullptr)
    {
        auto fromIt = zone->find (fromDegree);
        if (fromIt != zone->end())
        {
            auto toIt = fromIt->second.find (toDegree);
            if (toIt != fromIt->second.end())
                return toIt->second;
        }
    }

    // Fallback: original first-order model
    auto moodIt = firstOrder.find (moodStr);
    if (moodIt != firstOrder.end())
    {
        auto fromIt = moodIt->second.find (fromDegree);
        if (fromIt != moodIt->second.end())
        {
            auto toIt = fromIt->second.find (toDegree);
            if (toIt != fromIt->second.end())
                return toIt->second;
        }
    }

    return 1.0f / 7.0f;
}

// ═════════════════════════════════════════════════════════════════════════
// Anti-loop penalty
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::antiLoopPenalty (int degree, const std::vector<int>& recent)
{
    int sz = (int) recent.size();
    if (sz >= 1 && recent[(size_t)(sz - 1)] == degree) return -0.4f;
    if (sz >= 2 && recent[(size_t)(sz - 2)] == degree) return -0.25f;
    return 0.0f;
}

// ═════════════════════════════════════════════════════════════════════════
// Beat position weighting
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::beatPositionWeight (int degree, float beatPos)
{
    if (beatPos < 0.0f) return 0.0f;
    if (beatPos < 0.5f || beatPos >= 3.5f)
    {
        if (degree == 1) return 0.4f;
        if (degree == 4) return 0.1f;
        if (degree == 5) return -0.2f;
    }
    else if (beatPos >= 2.0f && beatPos < 2.5f)
    {
        if (degree == 4) return 0.3f;
        if (degree == 6) return 0.25f;
        if (degree == 1) return -0.15f;
    }
    else if (beatPos >= 3.0f && beatPos < 3.5f)
    {
        if (degree == 5) return 0.4f;
        if (degree == 7) return 0.25f;
        if (degree == 1) return -0.3f;
    }
    return 0.0f;
}

// ═════════════════════════════════════════════════════════════════════════
// Phrase resolution score — pushes toward resolution as phrase lengthens
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::phraseScore (const juce::String& mood, int anchor,
                                      int candidate, int phrasePos)
{
    if (phrasePos < 2) return 0.0f;

    float urgency = juce::jmin (1.0f, (float)(phrasePos - 1) / 5.0f);

    struct Target { int degree; float weight; };
    static const std::map<std::string, std::vector<Target>> targets = {
        { "Bright",  { {1,1.0f}, {6,0.6f}, {4,0.4f} } },
        { "Warm",    { {1,1.0f}, {7,0.7f}, {4,0.5f} } },
        { "Dream",   { {1,0.8f}, {4,0.7f}, {2,0.5f} } },
        { "Deep",    { {1,1.0f}, {7,0.8f}, {4,0.6f} } },
        { "Hollow",  { {1,1.0f}, {6,0.7f}, {3,0.5f} } },
        { "Tender",  { {1,1.0f}, {4,0.6f}, {6,0.5f} } },
        { "Tense",   { {1,1.0f}, {5,0.9f}, {7,0.7f} } },
        { "Dusk",    { {1,1.0f}, {7,0.8f}, {4,0.6f} } },
        { "Crest",    { {1,1.0f}, {6,0.6f}, {4,0.4f} } },
        { "Nocturne", { {1,1.0f}, {7,0.8f}, {6,0.6f} } },
        { "Shimmer",  { {1,1.0f}, {7,0.7f}, {4,0.5f} } },
        { "Static",   { {1,0.8f}, {4,0.7f}, {5,0.6f} } },
    };

    float score = 0.0f;
    auto it = targets.find (mood.toStdString());
    if (it != targets.end())
        for (auto& t : it->second)
            if (candidate == t.degree)
                score += t.weight * urgency * 0.4f;

    return score;
}

// ═════════════════════════════════════════════════════════════════════════
// Surprise score — breaks loops with interesting degrees
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::surpriseScore (const juce::String& mood, int candidate,
                                        const std::vector<int>& anchorHistory,
                                        bool loopDetected)
{
    if (!loopDetected) return 0.0f;

    float score = 0.0f;

    // Recency bonus — reward degrees not in recent anchor history
    bool recentlyPlayed = false;
    for (int h : anchorHistory)
        if (h == candidate) { recentlyPlayed = true; break; }
    if (!recentlyPlayed) score += 0.5f;

    // Mood-specific interesting degrees that break loops well
    static const std::map<std::string, std::vector<int>> interesting = {
        { "Bright",  {2,3,6,7} },
        { "Warm",    {2,3,6} },
        { "Dream",   {2,3,7} },
        { "Deep",    {2,6,3} },
        { "Hollow",  {6,3,7} },
        { "Tender",  {2,4,6} },
        { "Tense",   {6,3,7} },
        { "Dusk",    {2,6,3} },
        { "Crest",    {2,3,6,7} },
        { "Nocturne", {6,3,7} },
        { "Shimmer",  {2,3,6} },
        { "Static",   {2,6,7} },
    };

    auto it = interesting.find (mood.toStdString());
    if (it != interesting.end())
        for (int d : it->second)
            if (candidate == d) { score += 0.3f; break; }

    return score;
}

// ═════════════════════════════════════════════════════════════════════════
// Blended score — combines Markov, phrase, surprise with dynamic weights
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::computeBlendedScore (
    const juce::String& mood, int candidate,
    int anchor, int prevAnchor,
    int phrasePos, bool loopDetected,
    const std::vector<int>& anchorHistory,
    double /*bpm*/, float beatPos)
{
    float mScore = juce::jmax (0.01f, getTransitionProb (mood, prevAnchor, anchor, candidate));
    float pScore = phraseScore (mood, anchor, candidate, phrasePos);
    float sScore = surpriseScore (mood, candidate, anchorHistory, loopDetected);
    float bScore = beatPositionWeight (candidate, beatPos);
    float aScore = antiLoopPenalty (candidate, anchorHistory);

    // Dynamic weights based on phrase position and loop state
    float markovW, phraseW, surpriseW;

    if (loopDetected)
    {
        markovW   = 0.20f;
        phraseW   = 0.20f;
        surpriseW = 0.60f;
    }
    else if (phrasePos >= 6)
    {
        markovW   = 0.30f;
        phraseW   = 0.45f;
        surpriseW = 0.25f;
    }
    else if (phrasePos >= 3)
    {
        markovW   = 0.50f;
        phraseW   = 0.35f;
        surpriseW = 0.15f;
    }
    else
    {
        markovW   = 0.80f;
        phraseW   = 0.15f;
        surpriseW = 0.05f;
    }

    float total = (mScore  * markovW)
                + (pScore  * phraseW)
                + (sScore  * surpriseW)
                + (bScore  * 0.15f)
                + aScore;

    return juce::jmax (0.0f, total);
}

// ═════════════════════════════════════════════════════════════════════════
// getSuggestions — main entry point
// ═════════════════════════════════════════════════════════════════════════

SuggestionEngine::Suggestion SuggestionEngine::getSuggestions (
    const juce::String& mood, int anchorDegree, int prevAnchorDegree,
    double bpm, float beatPosition, int phrasePosition,
    bool loopDetected, const std::vector<int>& anchorHistory)
{
    if (anchorDegree < 1 || anchorDegree > 7)
        return { -1, -1 };

    // Score all candidates
    std::map<int, float> scores;
    for (int deg = 1; deg <= 7; ++deg)
    {
        if (deg == anchorDegree) continue;
        scores[deg] = computeBlendedScore (mood, deg, anchorDegree, prevAnchorDegree,
                                            phrasePosition, loopDetected, anchorHistory,
                                            bpm, beatPosition);
    }

    // Primary: highest score
    int primary = -1;
    float bestScore = -1.0f;
    for (auto& kv : scores)
    {
        if (kv.second > bestScore)
        {
            bestScore = kv.second;
            primary = kv.first;
        }
    }

    // Secondary: prefer different quality from primary, at least 30% of primary score
    // Quality lookup for degrees: major triads are typically 1,4,5; minor are 2,3,6; dim is 7
    auto qualityGroup = [] (int deg) -> int {
        if (deg == 1 || deg == 4 || deg == 5) return 0;  // major-ish
        if (deg == 2 || deg == 3 || deg == 6) return 1;  // minor-ish
        return 2;  // dim/aug
    };

    int primaryGroup = qualityGroup (primary);
    float minSecondary = bestScore * 0.3f;

    int secondary = -1;
    float secondBest = -1.0f;
    for (auto& kv : scores)
    {
        if (kv.first == primary) continue;
        if (kv.second < minSecondary) continue;

        // Contrast bonus: different quality group from primary
        float bonus = (qualityGroup (kv.first) != primaryGroup) ? 0.15f : 0.0f;
        float total = kv.second + bonus;

        if (total > secondBest)
        {
            secondBest = total;
            secondary = kv.first;
        }
    }

    return { primary, secondary };
}

// ═════════════════════════════════════════════════════════════════════════
// Progression detection (unchanged)
// ═════════════════════════════════════════════════════════════════════════

struct ProgressionSig { int degrees[5]; int len; const char* name; };

static const ProgressionSig kProgressions[] = {
    { {1,5,6,4}, 4, "pop arc" },
    { {1,4,5,1}, 4, "classic loop" },
    { {1,6,4,5}, 4, "soul changes" },
    { {6,4,1,5}, 4, "minor lift" },
    { {1,7,6,7}, 4, "modal drift" },
    { {1,7,3,7}, 4, "minor loop" },
    { {5,4,1,0}, 3, "rock engine" },
    { {6,3,1,5}, 4, "minor cadence" },
    { {1,5,4,5}, 4, "suspended drive" },
    { {2,5,1,0}, 3, "jazz turn" },
    { {1,6,3,7}, 4, "bittersweet" },
    { {5,6,4,1}, 4, "anthemic" },
    { {1,3,4,4}, 4, "bright climb" },
    { {6,5,4,3}, 4, "descending minor" },
};

juce::String SuggestionEngine::detectProgression (const std::vector<int>& recent)
{
    int sz = (int) recent.size();
    for (auto& sig : kProgressions)
    {
        if (sz < sig.len) continue;
        bool match = true;
        for (int i = 0; i < sig.len; ++i)
        {
            if (recent[(size_t)(sz - sig.len + i)] != sig.degrees[i])
            {
                match = false;
                break;
            }
        }
        if (match) return sig.name;
    }
    return {};
}
