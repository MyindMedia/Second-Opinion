include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

# Headless unit tests: link only so_core (no JUCE).
add_executable(so_unit_tests
    unit/theory_pitch_test.cpp
    unit/theory_parser_namer_test.cpp
    unit/theory_roundtrip_test.cpp
    unit/theory_key_roman_test.cpp
    unit/midi_writer_test.cpp
    unit/analysis_dsp_test.cpp
    unit/analysis_chords_test.cpp
    unit/analysis_melody_test.cpp
    unit/analysis_pipeline_test.cpp
    unit/analysis_tempo_test.cpp
    unit/analysis_accuracy_test.cpp
    unit/reharm_candidates_test.cpp
    unit/reharm_voicing_test.cpp
    unit/reharm_generate_test.cpp
    unit/audio_fifo_test.cpp
    unit/audio_capture_math_test.cpp
    unit/audio_preview_synth_test.cpp
    unit/song_mood_test.cpp
    unit/song_json_test.cpp
    unit/song_builder_test.cpp)
target_link_libraries(so_unit_tests PRIVATE so_core Catch2::Catch2WithMain)
so_set_warnings(so_unit_tests)
catch_discover_tests(so_unit_tests)

# Real-time safety: run processBlock under an allocation trap (PRD §10).
# Links the plugin's shared code (which already contains the JUCE module objects) and borrows its
# include paths and definitions, so JUCE isn't compiled a second time into the test.
add_executable(so_rt_tests
    rt/realtime_safety_test.cpp
    rt/capture_preview_rt_test.cpp
    rt/audio_file_loader_test.cpp
    rt/bridge_json_test.cpp)
target_link_libraries(so_rt_tests PRIVATE SecondOpinion Catch2::Catch2)
target_include_directories(so_rt_tests PRIVATE ${CMAKE_SOURCE_DIR}/src $<TARGET_PROPERTY:SecondOpinion,INCLUDE_DIRECTORIES>)
target_compile_definitions(so_rt_tests PRIVATE $<TARGET_PROPERTY:SecondOpinion,COMPILE_DEFINITIONS>)
target_compile_features(so_rt_tests PRIVATE cxx_std_20)
catch_discover_tests(so_rt_tests)

# Basic Pitch on ONNX Runtime (P1-08): separate executable so so_unit_tests stays ORT-free.
if(TARGET so_ml)
    add_executable(so_ml_tests unit/basic_pitch_test.cpp)
    target_link_libraries(so_ml_tests PRIVATE so_ml Catch2::Catch2WithMain)
    target_compile_definitions(so_ml_tests PRIVATE SO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    so_set_warnings(so_ml_tests)
    if(WIN32)
        # Must run before catch_discover_tests' post-build listing.
        so_bundle_onnxruntime(so_ml_tests "$<TARGET_FILE_DIR:so_ml_tests>")
    endif()
    catch_discover_tests(so_ml_tests)
endif()
