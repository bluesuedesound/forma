#include "SuggestionEngine.h"

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
// Markov transition probability (second-order → first-order → uniform)
// ═════════════════════════════════════════════════════════════════════════

float SuggestionEngine::getTransitionProb (const juce::String& mood,
                                            int prevDegree, int fromDegree, int toDegree)
{
    std::string moodStr = mood.toStdString();

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
