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
    unit/reharm_candidates_test.cpp
    unit/reharm_voicing_test.cpp
    unit/reharm_generate_test.cpp)
target_link_libraries(so_unit_tests PRIVATE so_core Catch2::Catch2WithMain)
so_set_warnings(so_unit_tests)
catch_discover_tests(so_unit_tests)

# Real-time safety: run processBlock under an allocation trap (PRD §10).
# Links the plugin's shared code (which already contains the JUCE module objects) and borrows its
# include paths and definitions, so JUCE isn't compiled a second time into the test.
add_executable(so_rt_tests rt/realtime_safety_test.cpp)
target_link_libraries(so_rt_tests PRIVATE SecondOpinion Catch2::Catch2)
target_include_directories(so_rt_tests PRIVATE ${CMAKE_SOURCE_DIR}/src $<TARGET_PROPERTY:SecondOpinion,INCLUDE_DIRECTORIES>)
target_compile_definitions(so_rt_tests PRIVATE $<TARGET_PROPERTY:SecondOpinion,COMPILE_DEFINITIONS>)
target_compile_features(so_rt_tests PRIVATE cxx_std_20)
catch_discover_tests(so_rt_tests)
