#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <atomic>

class SuggestionEngine
{
public:
    void loadModel (const juce::var& modelData);

    // Zone system — call once at startup after loadModel()
    void loadZonesFromBinaryData();

    // Call whenever mood or color changes (safe from any thread)
    void updateZone (int moodIndex, float colorAmount);

    struct Suggestion { int primary = -1, secondary = -1; };

    Suggestion getSuggestions (const juce::String& mood,
                               int anchorDegree,
                               int prevAnchorDegree,
                               double bpm,
                               float beatPosition,
                               int phrasePosition,
                               bool loopDetected,
                               const std::vector<int>& anchorHistory);

    // Progression detection
    juce::String detectProgression (const std::vector<int>& recent);

private:
    // first_order[mood][fromDeg][toDeg] = probability
    std::map<std::string,
        std::map<int, std::map<int, float>>> firstOrder;

    // second_order[mood][pairKey][toDeg] = probability
    std::map<std::string,
        std::map<std::string, std::map<int, float>>> secondOrder;

    // Zone system
    using FirstOrderModel = std::map<int, std::map<int, float>>;
    std::map<juce::String, FirstOrderModel> zoneModels;       // all zones, preloaded
    std::map<juce::String, int> moodZoneCounts;               // mood → max zone number
    juce::String currentZoneKey;
    std::atomic<const FirstOrderModel*> activeZoneModel { nullptr };
    bool zonesLoaded = false;

    juce::String getZoneKey (int moodIndex, float colorAmount) const;
    static int romanToInt (const juce::String& roman);

    float getTransitionProb (const juce::String& mood,
                              int prevDegree, int fromDegree, int toDegree);

    float phraseScore (const juce::String& mood, int anchor, int candidate, int phrasePos);
    float surpriseScore (const juce::String& mood, int candidate,
                          const std::vector<int>& anchorHistory, bool loopDetected);
    float computeBlendedScore (const juce::String& mood, int candidate,
                                int anchor, int prevAnchor,
                                int phrasePos, bool loopDetected,
                                const std::vector<int>& anchorHistory,
                                double bpm, float beatPos);

    float antiLoopPenalty (int degree, const std::vector<int>& recent);
    float beatPositionWeight (int degree, float beatPos);
};
