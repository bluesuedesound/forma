#pragma once
#include <juce_core/juce_core.h>

// Bass generator. Three modes:
//   0 Root            — root of current chord fires on chord press (inline
//                       from FormaProcessor::triggerChord).
//   1 Kick Trigger    — root fires whenever an incoming trigger note arrives.
//   2 Kick + Variation — root on downbeat triggers, possible variation on
//                       offbeat triggers (controlled by variationAmount).
//
// BassEngine itself does not emit per-block events; it only holds chord
// context and computes the pitch for a given trigger. The processor's MIDI
// trigger path in processBlock calls chooseTriggerNote() to resolve the
// pitch at the moment a trigger note is received.
class BassEngine
{
public:
    void prepareToPlay (double sr) { sampleRate = sr; }

    void setMode (int m)             { mode = juce::jlimit (0, 2, m); }
    void setOctaveOffset (int o)     { octaveOffset = juce::jlimit (-2, 2, o); }
    void setVariationAmount (float a){ variationAmount = juce::jlimit (0.0f, 1.0f, a); }

    // Chord context — called from triggerChord on every new chord press.
    void setCurrentChord (int degree, int rootMidi,
                          int fifthInterval, int thirdInterval)
    {
        this->degree = degree;
        this->rootMidi = rootMidi;
        this->fifthInterval = fifthInterval;
        this->thirdInterval = thirdInterval;
        chordActive = (degree >= 1);
    }

    void releaseChord() { chordActive = false; }

    int  getMode() const             { return mode; }
    bool isChordActive() const       { return chordActive; }

    // Returns MIDI pitch for a trigger at the given host beat position
    // (a real number; the fractional part indicates offbeat-ness).
    // Pass NaN or any negative value if beat position is unknown — treated
    // as downbeat (root).
    int chooseTriggerNote (double beatPosition, juce::Random& rng) const
    {
        if (! chordActive) return -1;

        int root = clampRange (rootMidi + octaveOffset * 12);

        if (mode != 2)  // Kick Trigger or Root — always root
            return root;

        // Kick + Variation: detect downbeat.
        bool onDownbeat = true;
        if (beatPosition >= 0.0)
        {
            double frac = beatPosition - std::floor (beatPosition);
            if (frac > 0.5) frac = 1.0 - frac;
            onDownbeat = (frac < 0.1);
        }

        if (onDownbeat) return root;

        // Offbeat: roll against variationAmount.
        if (rng.nextFloat() >= variationAmount) return root;

        // Pick a variation: root+oct-up / third / fifth.
        int pick = rng.nextInt (3);
        switch (pick)
        {
            case 0:  return clampRange (rootMidi + 12 + octaveOffset * 12);
            case 1:  return clampRange (rootMidi + thirdInterval + octaveOffset * 12);
            default: return clampRange (rootMidi + fifthInterval + octaveOffset * 12);
        }
    }

    // Kick + Variation also fires on downbeat chord-presses inline when the
    // mode is Root; KickTrigger/KickVariation modes suppress the chord-press
    // inline emission so triggers are the sole source.
    bool firesInlineOnChordPress() const { return mode == 0; }

private:
    int  mode = 0;
    int  octaveOffset = 0;
    float variationAmount = 0.30f;

    int  degree = -1;
    int  rootMidi = 36;
    int  fifthInterval = 7;
    int  thirdInterval = 4;
    bool chordActive = false;

    double sampleRate = 44100.0;

    static int clampRange (int midi) { return juce::jlimit (24, 72, midi); }
};
