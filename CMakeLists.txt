cmake_minimum_required(VERSION 3.22)
project(Forma VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Path to JUCE
add_subdirectory(/Users/alexupton/Downloads/JUCE "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(Forma
    COMPANY_NAME "Forma"
    BUNDLE_ID "com.forma.plugin"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_MIDI_INPUT TRUE
    PLUGIN_MANUFACTURER_CODE Frma
    PLUGIN_CODE Frm1
    FORMATS VST3
    VST3_CATEGORIES Instrument
    PRODUCT_NAME "Forma"
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
)

juce_add_binary_data(FormaData SOURCES
    Resources/forma_suggestions.json
)

target_sources(Forma PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/HarmonyEngine.cpp
    Source/HarmonyEngine.h
    Source/Arpeggiator.cpp
    Source/Arpeggiator.h
    Source/SuggestionEngine.cpp
    Source/SuggestionEngine.h
)

target_compile_definitions(Forma PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Forma
    PRIVATE
        juce::juce_audio_utils
        FormaData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ── Test executable ─────────────────────────────────────────────────────────

juce_add_console_app(FormaTest PRODUCT_NAME "FormaTest")

target_sources(FormaTest PRIVATE
    Source/HarmonyEngine.cpp
    Source/HarmonyEngine.h
    Source/TestHarmony.cpp
)

target_compile_definitions(FormaTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(FormaTest PRIVATE juce::juce_core)
