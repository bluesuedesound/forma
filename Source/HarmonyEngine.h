#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <deque>
#include <map>
#include <array>
#include <set>
#include <string>
#include <cmath>
#include <algorithm>
#include <climits>

class HarmonyEngine
{
public:
    HarmonyEngine();

    void setMood (const juce::String& mood);
    void setKey (int rootMidiNote);
    void setExtensionTier (int tier);
    void setColorAmount (float amount);
    void setVoicing (int v);
    void setFeelAmount (float f) { feelAmount = f; }

    std::vector<int> getChord (int degree);
    std::vector<int> getArpNotes (int degree);
    int getBassNote (int degree, bool altBass = false);

    // Voice leading — deterministic 4-voice assignment
    std::vector<int> getBestInversion (const std::vector<int>& chordTones,
                                       const std::vector<int>& prevVoices,
                                       float feel,
                                       int degree = -1);
    std::vector<int> placeNearRegister (const std::vector<int>& chordTones,
                                        int targetCenter);
    std::vector<int> applyVoicing (const std::vector<int>& notes, int voicing);
    std::vector<int> clampToRange (std::vector<int> notes);
    void resetVoiceLeadingState();

    int getColorTier (int degree, float colorAmount);

    juce::String getChordName (int degree);
    juce::String getChordShort (int degree);
    juce::String getMoodLabel();
    juce::String getChordQuality (int degree);

    static int getMoodRegisterTarget (const juce::String& mood);

    const juce::String& getCurrentMood() const { return currentMood; }
    int getCurrentMoodIndex() const
    {
        for (int i = 0; i < moodNames.size(); ++i)
            if (moodNames[i] == currentMood) return i;
        return 0;
    }
    int getRootMidi() const { return rootMidiNote; }
    int getVoicing() const { return voicingSetting; }
    float getColorAmount() const { return colorAmount; }
    const std::vector<int>& getScaleMidi() const { return scaleMidi; }
    int getTargetRegisterCenter() const { return targetRegisterCenter; }

    static const juce::StringArray moodNames;

private:
    struct ColorEntry { int baseTier; int coloredTier; float threshold; };

    juce::String currentMood = "Bright";
    int rootMidiNote = 48;
    int extensionTier = 1;
    int voicingSetting = 0;
    float colorAmount = 0.5f;
    float feelAmount  = 0.0f;

    std::vector<int> scaleIntervals;
    std::vector<juce::String> scaleQualities;
    std::vector<int> scaleMidi;

    std::array<std::map<int, std::vector<int>>, 7> extData;

    // Voice leading state
    int  targetRegisterCenter = 55;
    bool voiceCountSet        = false;

    // Register drift tracking
    std::deque<float> recentMidpoints;
    float currentDrift = 0.0f;

public:
    void updateDriftTracking (const std::vector<int>& voicedChord);
private:

    // Helpers
    std::vector<int> getUpperPCs (const std::vector<int>& chordTones);
    int findNearestOctave (int pc, int target, int lo, int hi);

    void buildScale();
    std::vector<int> voice (const std::vector<int>& notes);
    void computeExtensions (int degIdx);

    static std::vector<int> getTriad (const juce::String& quality);
    static ColorEntry getColorEntry (const juce::String& mood, int degree);
    static juce::String buildSuffix (const juce::String& quality,
                                     const std::vector<int>& exts);

    static const juce::String notesSharp[12];
    static const juce::String notesFlat[12];
};
