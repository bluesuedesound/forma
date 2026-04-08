#include "Arpeggiator.h"

// ── Expand chord to always span 2 octaves ──────────────────────────────────

void Arpeggiator::buildExpandedNotes()
{
    pool.clear();
    if (rawChord.empty()) return;

    auto sorted = rawChord;
    std::sort (sorted.begin(), sorted.end());

    // Apply octave offset
    for (auto& n : sorted) n += octOffset;

    // Base notes
    for (int n : sorted) pool.push_back (n);

    // Always add root + octave at top (ensures 2-octave span)
    pool.push_back (sorted[0] + 12);

    // For triads (3 or fewer), add third + octave for fullness
    if ((int) sorted.size() <= 3 && sorted.size() >= 2)
        pool.push_back (sorted[1] + 12);

    // Spread = 2: add another full octave
    if (spread >= 2)
    {
        int base = (int) sorted.size();
        for (int i = 0; i < base; ++i)
            pool.push_back (sorted[(size_t) i] + 24);
    }

    // Remove out-of-range and sort
    pool.erase (std::remove_if (pool.begin(), pool.end(),
        [] (int n) { return n < 21 || n > 108; }), pool.end());
    std::sort (pool.begin(), pool.end());

    // Remove duplicates
    pool.erase (std::unique (pool.begin(), pool.end()), pool.end());
}

static juce::String midiToName (int midi)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}

int Arpeggiator::resolveNote (int idx) const
{
    if (pool.empty()) return -1;
    if (idx < 0) return -1;  // rest

    int ps = (int) pool.size();
    int poolIdx = ((idx % ps) + ps) % ps;
    int note = pool[(size_t) poolIdx];

    DBG ("resolveNoteIndex: idx=" + juce::String (idx)
         + " poolSize=" + juce::String (ps)
         + " resolved=" + juce::String (note)
         + " name=" + midiToName (note));

    return note;
}

// ── Humanization ───────────────────────────────────────────────────────────

float Arpeggiator::humanizeDuration (float dur)
{
    float swing = (rng.nextFloat() - 0.5f) * 2.0f * feel * 0.15f;
    float rand  = (rng.nextFloat() - 0.5f) * 2.0f * drift * 0.10f;
    return dur * (1.0f + swing + rand);
}

float Arpeggiator::humanizeVelocity (float vel)
{
    float var = (rng.nextFloat() - 0.5f) * 2.0f * feel * 12.0f;
    return juce::jlimit (1.0f, 127.0f, vel + var);
}

// ── Build motif sequences ──────────────────────────────────────────────────

void Arpeggiator::buildSequence()
{
    steps.clear();
    if (pool.empty()) return;

    int ps = (int) pool.size();
    // Clamp indices: use modular indexing via resolveNote

    switch (pattern)
    {
        case Pattern::Rise:
        {
            // Ascending through all pool notes, root accented
            for (int i = 0; i < ps; ++i)
                steps.push_back ({ i, 1.0f, (i == 0) ? 1.3f : 0.9f, 1.0f });
            // Land on top note with accent
            if (ps > 1) steps.back().velMult = 1.1f;
            break;
        }

        case Pattern::Cascade:
        {
            // Descend from top, root repeats short then long
            for (int i = ps - 1; i >= 0; --i)
                steps.push_back ({ i, 1.0f, 0.85f, 1.0f });
            steps.push_back ({ 0, 0.5f, 1.2f, 1.0f });  // root short accent
            steps.push_back ({ 0, 1.5f, 0.7f, 1.0f });   // root long decay
            break;
        }

        case Pattern::Pulse:
        {
            // Root strong, two ghosts, rest — soul pocket
            steps.push_back ({ 0,                  1.0f, 1.4f,  1.0f });  // root
            steps.push_back ({ juce::jmin (2, ps-1), 0.5f, 0.45f, 1.0f }); // fifth ghost
            steps.push_back ({ juce::jmin (1, ps-1), 0.5f, 0.45f, 1.0f }); // third ghost
            steps.push_back ({ -1,                 1.0f, 0.0f,  1.0f });  // rest
            break;
        }

        case Pattern::Groove:
        {
            // Syncopated — rests on downbeat, hits on offbeats
            steps.push_back ({ -1,                 0.5f, 0.0f, 1.0f });  // rest
            steps.push_back ({ 0,                  0.5f, 1.2f, 1.0f });  // root offbeat
            steps.push_back ({ -1,                 0.5f, 0.0f, 1.0f });  // rest
            steps.push_back ({ juce::jmin (2, ps-1), 0.5f, 0.9f, 1.0f }); // third
            steps.push_back ({ juce::jmin (1, ps-1), 0.5f, 1.0f, 1.0f }); // second
            steps.push_back ({ -1,                 0.5f, 0.0f, 1.0f });  // rest
            steps.push_back ({ 0,                  0.5f, 1.1f, 1.0f });  // root
            steps.push_back ({ juce::jmin (ps-1, ps-1), 0.5f, 0.8f, 1.0f }); // high note
            break;
        }

        case Pattern::Spiral:
        {
            // Low-high-sustained-low-sustained
            steps.push_back ({ 0,                  0.5f, 1.0f, 1.0f });  // low root
            steps.push_back ({ ps - 1,             0.5f, 0.8f, 1.0f });  // highest
            steps.push_back ({ juce::jmin (2, ps-1), 1.5f, 0.9f, 1.0f }); // mid sustained
            steps.push_back ({ juce::jmin (1, ps-1), 0.5f, 0.7f, 1.0f }); // second
            steps.push_back ({ juce::jmin (3, ps-1), 0.5f, 0.7f, 1.0f }); // fourth
            steps.push_back ({ 0,                  1.5f, 1.1f, 1.0f });  // root sustained
            break;
        }

        case Pattern::Drift:
        {
            // Probabilistic — each step may or may not fire
            steps.push_back ({ 0,                  1.5f, 1.0f,  0.95f }); // root almost always
            steps.push_back ({ juce::jmin (3, ps-1), 0.5f, 0.6f,  0.55f }); // upper sometimes
            steps.push_back ({ -1,                 1.0f, 0.0f,  1.0f  }); // rest always
            steps.push_back ({ juce::jmin (2, ps-1), 1.0f, 0.8f,  0.70f }); // mid usually
            steps.push_back ({ juce::jmin (1, ps-1), 0.5f, 0.5f,  0.40f }); // second rarely
            steps.push_back ({ ps - 1,             1.5f, 0.9f,  0.65f }); // high often
            steps.push_back ({ -1,                 1.0f, 0.0f,  1.0f  }); // rest always
            break;
        }
    }
}

// ── Sample-accurate processing ─────────────────────────────────────────────

std::vector<Arpeggiator::ArpEvent> Arpeggiator::process (int numSamples,
                                                           double bpm,
                                                           double sampleRate)
{
    std::vector<ArpEvent> events;
    if (! active || steps.empty() || pool.empty()) return events;

    // Rate defines steps-per-beat:  0.5x=2(8ths), 1.0x=4(16ths), 2.0x=8(32nds)
    double beatSamples = (sampleRate * 60.0 / bpm);
    double stepsPerBeat = 4.0 * (double) rateMult;
    double baseSamplesPerStep = beatSamples / stepsPerBeat;

    for (int i = 0; i < numSamples; ++i)
    {
        auto& step = steps[(size_t)(stepIdx % (int) steps.size())];

        // Step duration — variable per step, humanized
        float humanDur = humanizeDuration (step.duration);
        double stepSamples = baseSamplesPerStep * (double) humanDur;
        if (stepSamples < 1.0) stepSamples = 1.0;

        double phaseInc = 1.0 / stepSamples;
        stepPhase += phaseInc;

        // Note-off at gate point
        if (pendingOff >= 0 && stepPhase >= (double) gate)
        {
            events.push_back ({ pendingOff, 0, false, i });
            pendingOff = -1;
        }

        // Step boundary — advance
        if (stepPhase >= 1.0)
        {
            stepPhase -= 1.0;
            stepIdx++;

            auto& next = steps[(size_t)(stepIdx % (int) steps.size())];

            // Release previous if still sounding
            if (pendingOff >= 0)
            {
                events.push_back ({ pendingOff, 0, false, i });
                pendingOff = -1;
            }

            if (next.noteIdx >= 0)
            {
                // Probability check (Drift motif)
                float prob = next.probability;
                if (pattern == Pattern::Drift)
                    prob -= drift * 0.15f;  // higher drift = more unpredictable

                bool fires = (rng.nextFloat() < prob);
                if (fires)
                {
                    int note = resolveNote (next.noteIdx);
                    if (note >= 0)
                    {
                        float rawVel = 80.0f * next.velMult;
                        int vel = (int) humanizeVelocity (rawVel);
                        events.push_back ({ note, vel, true, i });
                        pendingOff = note;

                        DBG ("Arp step " + juce::String (stepIdx % (int) steps.size())
                             + ": " + midiToName (note) + " (" + juce::String (note)
                             + ") vel=" + juce::String (vel));
                    }
                }
            }
            // else: rest — do nothing
        }
    }

    return events;
}
