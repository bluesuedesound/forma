#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

class Arpeggiator
{
public:
    enum class Pattern { Rise, Cascade, Pulse, Groove, Spiral, Drift };

    struct ArpEvent {
        int note, velocity;
        bool isNoteOn;
        int sampleOffset;
    };

    // ── Setters ──
    void setPattern (Pattern p)          { pattern = p; buildSequence(); }
    void setRate (float mult)            { rateMult = juce::jlimit (0.25f, 4.0f, mult); }
    void setGate (float g)               { gate = juce::jlimit (0.05f, 0.99f, g); }
    void setSpread (int oct)             { spread = juce::jlimit (1, 2, oct); buildSequence(); }
    void setOctaveOffset (int semi)      { octOffset = juce::jlimit (-24, 24, semi); buildSequence(); }
    void setActive (bool a)              { active = a; }
    void setFeelAmount (float f)         { feel = f; }
    void setDriftAmount (float d)        { drift = d; }

    bool isActive() const                { return active && ! steps.empty(); }
    Pattern getPattern() const           { return pattern; }
    float getRate() const                { return rateMult; }
    float getGate() const                { return gate; }
    int   getSpread() const              { return spread; }
    int   getOctaveOffset() const        { return octOffset; }

    void setChord (const std::vector<int>& sorted)
    {
        rawChord = sorted;
        buildExpandedNotes();
        buildSequence();
        stepIdx = 0;
    }

    void reset()
    {
        stepPhase = 0.0;
        stepIdx = 0;
        pendingOff = -1;
    }

    int getPendingNoteOff() const { return pendingOff; }
    int flushPendingOff()         { int n = pendingOff; pendingOff = -1; return n; }

    std::vector<ArpEvent> process (int numSamples, double bpm, double sampleRate);

private:
    // ── Step definition ──
    struct Step {
        int   noteIdx;     // index into expandedNotes, -1 = rest
        float duration;    // relative beat units
        float velMult;     // velocity multiplier
        float probability; // 1.0 = always, <1.0 = probabilistic (Drift motif)
    };

    Pattern pattern = Pattern::Rise;
    float rateMult = 1.0f;
    float gate = 0.8f;
    int   spread = 1;
    int   octOffset = 0;
    bool  active = false;
    float feel = 0.0f;
    float drift = 0.0f;

    std::vector<int> rawChord;
    std::vector<int> pool;  // expanded note pool
    std::vector<Step> steps;
    int    stepIdx = 0;
    double stepPhase = 0.0;
    int    pendingOff = -1;

    juce::Random rng;

    void buildExpandedNotes();
    void buildSequence();
    int  resolveNote (int idx) const;
    float humanizeDuration (float dur);
    float humanizeVelocity (float vel);
};
