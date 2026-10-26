cmake_minimum_required(VERSION 3.25)

# Universal binary on macOS unless the preset says otherwise. Must be set before project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version (open question Q9)")

project(SecondOpinion VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Sources contain UTF-8 literals (♭ ♯ ° ø Δ); make MSVC read them as UTF-8.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

option(SO_BUILD_TESTS "Build unit and real-time safety tests" ON)
option(SO_ENABLE_AAX "Build the AAX format (later; needs the Avid SDK and PACE)" OFF)
option(SO_COPY_PLUGIN "Copy the plugin to the system plugin folder after building" OFF)
option(SO_WEBVIEW2_STATIC "Windows: link WebView2 statically (needs the Microsoft.Web.WebView2 NuGet package)" ON)

include(cmake/CPM.cmake)
include(cmake/Warnings.cmake)
include(cmake/OnnxRuntime.cmake)   # option SO_ENABLE_ONNX, target so::onnxruntime (ADR 0009)

# ---------------------------------------------------------------------------------------------
# Dependencies. Pin exact versions so builds are reproducible.
# Offline or air-gapped builds can point CPM at local checkouts:
#   -DCPM_JUCE_SOURCE=/path/to/JUCE -DCPM_Catch2_SOURCE=/path/to/Catch2
# ---------------------------------------------------------------------------------------------
CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE)

if(SO_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        VERSION 3.7.1
        GIT_SHALLOW TRUE)
endif()

# ---------------------------------------------------------------------------------------------
# so_core: headless C++ (no JUCE GUI). Theory, MIDI writing, later analysis and reharm.
# ---------------------------------------------------------------------------------------------
add_library(so_core STATIC
    src/core/theory/Pitch.cpp
    src/core/theory/ChordQuality.cpp
    src/core/theory/Chord.cpp
    src/core/theory/ChordSymbolParser.cpp
    src/core/theory/ChordNamer.cpp
    src/core/theory/Key.cpp
    src/core/theory/RomanNumeral.cpp
    src/core/midi/MidiWriter.cpp
    src/core/midi/SimpleVoicer.cpp
    src/core/analysis/FFT.cpp
    src/core/analysis/Resampler.cpp
    src/core/analysis/ChromaExtractor.cpp
    src/core/analysis/BeatAggregator.cpp
    src/core/analysis/ChordDecoder.cpp
    src/core/analysis/KeyEstimator.cpp
    src/core/analysis/MelodyTracker.cpp
    src/core/analysis/Analyzer.cpp
    src/core/analysis/TempoEstimator.cpp
    src/core/reharm/Harmony.cpp
    src/core/reharm/CandidateGenerator.cpp
    src/core/reharm/VoiceLeadingSolver.cpp
    src/core/reharm/BassGenerator.cpp
    src/core/reharm/Explainer.cpp
    src/core/reharm/Ranker.cpp
    src/core/reharm/Reharmonizer.cpp
    src/core/audio/CaptureMath.cpp
    src/core/preview/PreviewSynth.cpp
    src/core/song/SongTypes.cpp
    src/core/song/MiniJson.cpp
    src/core/song/MoodProfile.cpp
    src/core/song/MoodInterpreter.cpp
    src/core/song/SongStructure.cpp
    src/core/song/SongBuilder.cpp
    src/core/song/SongMidi.cpp)
target_include_directories(so_core PUBLIC src)
so_set_warnings(so_core)

# Console demo of the song builder (no JUCE).
add_executable(so_song_demo tools/song_demo/main.cpp)
target_link_libraries(so_song_demo PRIVATE so_core)
so_set_warnings(so_song_demo)

# ---------------------------------------------------------------------------------------------
# so_ml: Basic Pitch transcription on ONNX Runtime (no JUCE). Kept out of so_core so the core and
# its tests never depend on onnxruntime.
# ---------------------------------------------------------------------------------------------
if(SO_ENABLE_ONNX)
    add_library(so_ml STATIC src/core/analysis/BasicPitchRunner.cpp)
    target_link_libraries(so_ml PUBLIC so_core so::onnxruntime)
    so_set_warnings(so_ml)
    if(WIN32 AND NOT SO_ORT_MINIMAL)
        # The runner loads onnxruntime.dll by full path from the plugin folder; the import is only
        # a fallback, so it must not be resolved when the plugin DLL loads.
        target_link_options(so_ml INTERFACE "/DELAYLOAD:onnxruntime.dll")
        target_link_libraries(so_ml INTERFACE delayimp)
    endif()
    set(SO_BASIC_PITCH_MODEL ${CMAKE_SOURCE_DIR}/models/basic_pitch/nmp.onnx)
    if(SO_ORT_MINIMAL)
        # The minimal runtime only reads ORT format; ship the converted model under the same name.
        configure_file(${SO_ORT_MINIMAL_MODEL} ${CMAKE_BINARY_DIR}/basic_pitch_ort/nmp.onnx COPYONLY)
        set(SO_BASIC_PITCH_MODEL ${CMAKE_BINARY_DIR}/basic_pitch_ort/nmp.onnx)
    endif()
    set(SO_BASIC_PITCH_FILES
        ${SO_BASIC_PITCH_MODEL}
        ${CMAKE_SOURCE_DIR}/models/basic_pitch/LICENSE
        ${CMAKE_SOURCE_DIR}/models/basic_pitch/NOTICE)
endif()

# Analysis CLI for the chord evaluation (P2-05, tools/eval). No JUCE; Basic Pitch when so_ml exists.
add_executable(so_analyze_cli tools/analyze_cli/main.cpp)
target_include_directories(so_analyze_cli PRIVATE tools/analyze_cli)
if(TARGET so_ml)
    target_link_libraries(so_analyze_cli PRIVATE so_ml)
    target_compile_definitions(so_analyze_cli PRIVATE SO_HAS_ONNX=1)
else()
    target_link_libraries(so_analyze_cli PRIVATE so_core)
endif()
so_set_warnings(so_analyze_cli)

# ---------------------------------------------------------------------------------------------
# WebView UI: ui/ (Vite + TypeScript) → ui/dist → ui.zip → BinaryData
# ---------------------------------------------------------------------------------------------
include(cmake/WebUI.cmake)
so_add_web_ui(SecondOpinionUI)

# ---------------------------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------------------------
set(SO_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SO_FORMATS AU)
endif()
if(SO_ENABLE_AAX)
    list(APPEND SO_FORMATS AAX)
endif()

set(SO_WEBVIEW2_ARG "")
if(WIN32 AND SO_WEBVIEW2_STATIC)
    set(SO_WEBVIEW2_ARG NEEDS_WEBVIEW2 TRUE)
endif()

# Placeholders until Lawrence confirms (docs/OPEN_QUESTIONS.md, Q1).
juce_add_plugin(SecondOpinion
    PRODUCT_NAME "Second Opinion"
    COMPANY_NAME "Berment Audio"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.bermentaudio.secondopinion
    PLUGIN_MANUFACTURER_CODE Brmt
    PLUGIN_CODE SOp1
    FORMATS ${SO_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${SO_COPY_PLUGIN}
    NEEDS_WEB_BROWSER TRUE
    # macOS Standalone: NSMicrophoneUsageDescription, required to read the audio input at all.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Second Opinion listens to the audio input to read the chords you play."
    ${SO_WEBVIEW2_ARG})

target_sources(SecondOpinion PRIVATE
    src/plugin/Parameters.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/AudioFileLoader.cpp
    src/plugin/PluginEditor.cpp
    src/bridge/WebBridge.cpp
    src/bridge/NativeDragStrip.cpp
    src/bridge/Json.cpp
    src/services/SessionEngine.cpp
    src/services/SongBridge.cpp
    src/services/AiMoodClient.cpp)

target_include_directories(SecondOpinion PRIVATE src)

target_compile_definitions(SecondOpinion PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${SO_WEBVIEW2_STATIC}>>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)

target_link_libraries(SecondOpinion
    PRIVATE
        so_core
        SecondOpinionUI
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(SO_ENABLE_ONNX)
    target_link_libraries(SecondOpinion PRIVATE so_ml)
    target_compile_definitions(SecondOpinion PUBLIC SO_HAS_ONNX=1)
    so_bundle_basic_pitch(SecondOpinion ${SO_BASIC_PITCH_FILES})
endif()

# ---------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------
if(SO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
