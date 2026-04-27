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
    Resources/zones/zones_index.json
    Resources/zones/bright_zone1.json
    Resources/zones/bright_zone2.json
    Resources/zones/bright_zone3.json
    Resources/zones/bright_zone4.json
    Resources/zones/warm_zone1.json
    Resources/zones/warm_zone2.json
    Resources/zones/warm_zone3.json
    Resources/zones/dream_zone1.json
    Resources/zones/dream_zone2.json
    Resources/zones/deep_zone1.json
    Resources/zones/deep_zone2.json
    Resources/zones/deep_zone3.json
    Resources/zones/deep_zone4.json
    Resources/zones/hollow_zone1.json
    Resources/zones/hollow_zone2.json
    Resources/zones/hollow_zone3.json
    Resources/zones/tender_zone1.json
    Resources/zones/tender_zone2.json
    Resources/zones/tender_zone3.json
    Resources/zones/tense_zone1.json
    Resources/zones/tense_zone2.json
    Resources/zones/tense_zone3.json
    Resources/zones/dusk_zone1.json
    Resources/zones/dusk_zone2.json
    Resources/zones/dusk_zone3.json
    Resources/zones/dusk_zone4.json
    Resources/packs/bright_lights/pack_index.json
    Resources/packs/bright_lights/crest_zone1.json
    Resources/packs/bright_lights/crest_zone2.json
    Resources/packs/bright_lights/crest_zone3.json
    Resources/packs/bright_lights/crest_zone4.json
    Resources/packs/bright_lights/nocturne_zone1.json
    Resources/packs/bright_lights/nocturne_zone2.json
    Resources/packs/bright_lights/nocturne_zone3.json
    Resources/packs/bright_lights/shimmer_zone1.json
    Resources/packs/bright_lights/shimmer_zone2.json
    Resources/packs/bright_lights/shimmer_zone3.json
    Resources/packs/bright_lights/static_zone1.json
    Resources/packs/bright_lights/static_zone2.json
    Resources/packs/bright_lights/static_zone3.json
)

target_sources(Forma PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/HarmonyEngine.cpp
    Source/HarmonyEngine.h
    Source/BassEngine.cpp
    Source/BassEngine.h
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
