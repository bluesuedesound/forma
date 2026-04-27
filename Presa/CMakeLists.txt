cmake_minimum_required(VERSION 3.22)
project(Presa VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Path to JUCE
add_subdirectory(/Users/alexupton/Downloads/JUCE "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(Presa
    COMPANY_NAME "Forma"
    BUNDLE_ID "com.forma.presa"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE Frma
    PLUGIN_CODE Prs1
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx Tools
    PRODUCT_NAME "Presa"
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
    HARDENED_RUNTIME_ENABLED TRUE
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict><key>NSScreenCaptureUsageDescription</key><string>Presa captures system audio to use as sampler input.</string></dict></plist>"
)

target_sources(Presa PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/FormaPalette.h
    Source/FormaLookAndFeel.cpp
    Source/FormaLookAndFeel.h
    Source/WaveformDisplay.cpp
    Source/WaveformDisplay.h
    Source/StateDotField.cpp
    Source/StateDotField.h
    Source/PadGrid.cpp
    Source/PadGrid.h
    Source/CaptureBuffer.cpp
    Source/CaptureBuffer.h
    Source/capture/LoopbackCapture.h
    Source/capture/ScreenCaptureAudio.h
    Source/sampler/SamplePlayer.cpp
    Source/sampler/SamplePlayer.h
    Source/fx/TapeSaturator.cpp
    Source/fx/TapeSaturator.h
    Source/fx/DegradeProcessor.cpp
    Source/fx/DegradeProcessor.h
    Source/fx/FilterModule.cpp
    Source/fx/FilterModule.h
    Source/fx/ReverbModule.cpp
    Source/fx/ReverbModule.h
    Source/fx/AnalogDelay.cpp
    Source/fx/AnalogDelay.h
    Source/fx/FXChain.cpp
    Source/fx/FXChain.h
    Source/fx/AmbientEngine.cpp
    Source/fx/AmbientEngine.h
)

# Platform-specific loopback capture implementation
if(APPLE)
    target_sources(Presa PRIVATE
        Source/capture/LoopbackCapture_Mac.cpp
        Source/capture/ScreenCaptureAudio.mm)
    # ScreenCaptureKit requires macOS 13+.
    set_target_properties(Presa PROPERTIES
        OSX_DEPLOYMENT_TARGET "13.0")
    target_link_libraries(Presa PRIVATE
        "-framework ScreenCaptureKit"
        "-framework CoreMedia"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework Foundation")
elseif(WIN32)
    target_sources(Presa PRIVATE Source/capture/LoopbackCapture_Win.cpp)
endif()

target_compile_definitions(Presa PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Needed for the Standalone export flow: AlertWindow::runModalLoop() is
    # only compiled in when modal loops are permitted. Safe here because the
    # one call site is guarded by JucePlugin_Build_Standalone, so the VST3
    # target never reaches it.
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# Codesign Standalone with entitlements — required for ScreenCaptureKit
# permission prompts to persist across launches from Finder/Spotlight.
if(APPLE)
    add_custom_command(TARGET Presa_Standalone POST_BUILD
        COMMAND codesign --force --deep --sign - --entitlements "${CMAKE_SOURCE_DIR}/Presa.entitlements"
                "$<TARGET_BUNDLE_DIR:Presa_Standalone>"
        COMMENT "Codesigning Presa Standalone with screen-capture entitlement"
    )
endif()

target_link_libraries(Presa
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
