#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <string>
#include <cmath>

class SuggestionEngine
{
public:
    void loadModel (const juce::var& modelData);

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
