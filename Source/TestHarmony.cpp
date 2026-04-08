#include "HarmonyEngine.h"
#include <iostream>

static juce::String noteName (int midi)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}

int main()
{
    std::cout << "=== HarmonyEngine C++ Port Test ===\n\n";

    HarmonyEngine engine;
    engine.setMood ("Deep");
    engine.setKey (48);  // C3
    engine.setColorAmount (0.75f);

    // Expected from Python:
    //   Deep C degree 1 color=0.75: [48, 51, 55, 58, 62]
    //   Deep C degree 4 color=0.75: [53, 57, 60, 63, 67]
    //   Deep C degree 6 color=0.75: [57, 60, 63]

    struct TestCase { int degree; std::vector<int> expected; const char* label; };
    TestCase tests[] = {
        { 1, {48, 51, 55, 58, 62}, "i (Cm9)" },
        { 4, {53, 57, 60, 63, 67}, "IV (F9)" },
        { 6, {57, 60, 63},         "vi (Adim)" },
    };

    bool allPassed = true;

    for (auto& tc : tests)
    {
        auto chord = engine.getChord (tc.degree);

        std::cout << "  Deep C degree " << tc.degree << " " << tc.label << ": [";
        for (size_t i = 0; i < chord.size(); ++i)
        {
            if (i > 0) std::cout << ", ";
            std::cout << chord[i];
        }
        std::cout << "]  names=[";
        for (size_t i = 0; i < chord.size(); ++i)
        {
            if (i > 0) std::cout << ", ";
            std::cout << noteName (chord[i]).toStdString();
        }
        std::cout << "]" << std::endl;

        if (chord != tc.expected)
        {
            std::cout << "  FAIL — expected [";
            for (size_t i = 0; i < tc.expected.size(); ++i)
            {
                if (i > 0) std::cout << ", ";
                std::cout << tc.expected[i];
            }
            std::cout << "]" << std::endl;
            allPassed = false;
        }
    }

    // Test chord naming
    std::cout << "\n  Chord names:\n";
    for (int d = 1; d <= 7; ++d)
        std::cout << "    [" << d << "] " << engine.getChordName (d).toStdString() << std::endl;

    // Test voice leading
    std::cout << "\n  Voice leading test (I -> IV):\n";
    engine.setColorAmount (0.0f);
    auto chord1 = engine.getChord (1);
    auto chord4 = engine.getChord (4);
    auto voiced4 = engine.getBestInversion (chord4, chord1, 0.5f, 4);
    std::cout << "    I:         [";
    for (size_t i = 0; i < chord1.size(); ++i) { if (i) std::cout << ", "; std::cout << noteName(chord1[i]).toStdString(); }
    std::cout << "]\n    IV voiced:  [";
    for (size_t i = 0; i < voiced4.size(); ++i) { if (i) std::cout << ", "; std::cout << noteName(voiced4[i]).toStdString(); }
    std::cout << "]\n";

    // Test voicing spread
    std::cout << "\n  Voicing spread test (Cm triad):\n";
    engine.setColorAmount (0.0f);
    auto base = engine.getChord (1);
    for (int v : {-2, 0, 2, 5})
    {
        auto spread = engine.applyVoicing (base, v);
        std::cout << "    voicing=" << (v >= 0 ? "+" : "") << v << ": [";
        for (size_t i = 0; i < spread.size(); ++i)
        {
            if (i) std::cout << ", ";
            std::cout << noteName(spread[i]).toStdString();
        }
        int span = spread.empty() ? 0 : (spread.back() - spread.front());
        std::cout << "]  span=" << span << std::endl;
    }

    // Test all moods produce valid chords
    std::cout << "\n  All moods test:\n";
    for (auto& mood : HarmonyEngine::moodNames)
    {
        engine.setMood (mood);
        engine.setKey (48);
        engine.setColorAmount (0.5f);
        std::cout << "    " << mood.toStdString() << ": ";
        for (int d = 1; d <= 7; ++d)
        {
            auto c = engine.getChord (d);
            // Verify ascending
            for (size_t i = 1; i < c.size(); ++i)
            {
                if (c[i] <= c[i-1])
                {
                    std::cout << "FAIL (not ascending at deg " << d << ")";
                    allPassed = false;
                    break;
                }
            }
            std::cout << engine.getChordShort (d).toStdString() << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "\n" << (allPassed ? "  ALL TESTS PASSED" : "  SOME TESTS FAILED") << "\n\n";
    return allPassed ? 0 : 1;
}
