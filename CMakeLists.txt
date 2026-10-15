cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binaries")
endif()

project(TheCalm VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Development builds install the plugins for the current user on macOS; on Windows that would need
# admin rights (Program Files), so the installer does it instead.
if(APPLE)
    set(THECALM_COPY_AFTER_BUILD TRUE)
else()
    set(THECALM_COPY_AFTER_BUILD FALSE)
endif()

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(TheCalm
    COMPANY_NAME "BetaRabbit"
    PRODUCT_NAME "The Calm"
    BUNDLE_ID "com.betarabbit.thecalm"
    PLUGIN_MANUFACTURER_CODE Btrb
    PLUGIN_CODE Tclm
    FORMATS VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${THECALM_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)

clap_juce_extensions_plugin(TARGET TheCalm
    CLAP_ID "com.betarabbit.thecalm"
    CLAP_FEATURES audio-effect pitch-shifter distortion)

# The copy above replaces files inside the installed bundle but leaves the bundle folder's date alone,
# and hosts such as REAPER only rescan a CLAP when that date changes. Touch it after every install.
if(APPLE)
    add_custom_command(TARGET TheCalm_CLAP POST_BUILD
        COMMAND touch "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP/The Calm.clap"
        VERBATIM)
endif()

target_sources(TheCalm PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/MidiLearn.cpp)

target_include_directories(TheCalm PRIVATE Source)

# Optional full-window skin artwork (Resources/skin.png, 1024 x 1536). Without it the UI is drawn in code.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/skin.png")
    juce_add_binary_data(TheCalmAssets SOURCES Resources/skin.png)
    target_link_libraries(TheCalm PRIVATE TheCalmAssets)
    target_compile_definitions(TheCalm PRIVATE THECALM_HAS_ARTWORK=1)
endif()

target_compile_definitions(TheCalm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TheCalm
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Renders the editor to a PNG without a host: ui_snapshot out.png [width] [key=value ...]
juce_add_console_app(ui_snapshot PRODUCT_NAME "ui_snapshot")
target_sources(ui_snapshot PRIVATE tools/ui_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PresetManager.cpp Source/MidiLearn.cpp)
target_include_directories(ui_snapshot PRIVATE Source)
target_compile_definitions(ui_snapshot PRIVATE JucePlugin_Name="The Calm" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ui_snapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
if(TARGET TheCalmAssets)
    target_link_libraries(ui_snapshot PRIVATE TheCalmAssets)
    target_compile_definitions(ui_snapshot PRIVATE THECALM_HAS_ARTWORK=1)
endif()

# Self-checks for MIDI Learn, presets and parameter defaults.
juce_add_console_app(calm_tests PRODUCT_NAME "calm_tests")
target_sources(calm_tests PRIVATE tools/calm_tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PresetManager.cpp Source/MidiLearn.cpp)
target_include_directories(calm_tests PRIVATE Source)
target_compile_definitions(calm_tests PRIVATE JucePlugin_Name="The Calm" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(calm_tests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
if(TARGET TheCalmAssets)
    target_link_libraries(calm_tests PRIVATE TheCalmAssets)
    target_compile_definitions(calm_tests PRIVATE THECALM_HAS_ARTWORK=1)
endif()

# CPU benchmark and randomized stress test.
juce_add_console_app(calm_stress PRODUCT_NAME "calm_stress")
target_sources(calm_stress PRIVATE tools/calm_stress.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PresetManager.cpp Source/MidiLearn.cpp)
target_include_directories(calm_stress PRIVATE Source)
target_compile_definitions(calm_stress PRIVATE JucePlugin_Name="The Calm" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(calm_stress PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
if(TARGET TheCalmAssets)
    target_link_libraries(calm_stress PRIVATE TheCalmAssets)
    target_compile_definitions(calm_stress PRIVATE THECALM_HAS_ARTWORK=1)
endif()

# Offline renderer: runs the DSP core on WAV files without a host (used for A/B tests).
add_executable(calm_render tools/calm_render.cpp)
target_include_directories(calm_render PRIVATE Source)
