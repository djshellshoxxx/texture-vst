# The unit test runner (17_TESTING.md).
#
# Deliberately links the DSP, IO, preset and licence sources directly rather than
# the plugin target: the units under test take their sample rate and block size
# through prepare() and have no host coupling, which is what makes them testable.

juce_add_console_app(Texture_Tests
    PRODUCT_NAME "Texture Tests")

set(TEXTURE_TEST_SOURCES
    test_main.cpp
    test_motion_preferences.cpp
    test_versioned_random_streams.cpp
    test_state_schema.cpp
    test_control_value_parser.cpp
    test_control_value_clipboard.cpp
    test_midi_learn.cpp
    test_offline_render_timeline.cpp
    test_granular_engine.cpp
    test_sample_loader.cpp
    test_license_manager.cpp
    test_preset_manager.cpp
    test_parameter_locks.cpp
    test_mutation_planner.cpp
    test_legacy_render_fixtures.cpp
    test_tone_space.cpp
    test_fuzz.cpp
    TestHelpers.h

    ../Source/Engine/Grain.cpp
    ../Source/Engine/GranularEngine.cpp
    ../Source/Engine/SampleBuffer.cpp
    ../Source/Engine/Voice.cpp

    ../Source/Effects/Chorus.cpp
    ../Source/Effects/EffectsChain.cpp
    ../Source/Effects/Reverb.cpp
    ../Source/Effects/Saturator.cpp
    ../Source/Effects/SecretEffect.cpp
    ../Source/Effects/ToneSpace.cpp

    ../Source/Modulation/EnvelopeADSR.cpp
    ../Source/Modulation/MorphLFO.cpp

    ../Source/IO/FactoryContent.cpp
    ../Source/IO/SampleLoader.cpp
    ../Source/IO/WavExporter.cpp

    ../Source/Presets/LockPolicy.cpp
    ../Source/Presets/MutationPlanner.cpp
    ../Source/Presets/PresetManager.cpp

    ../Source/License/LicenseManager.cpp

    ../Source/Utility/Logging.cpp
    ../Source/Utility/MidiLearn.cpp
    ../Source/Utility/ParameterIDs.cpp)

target_sources(Texture_Tests PRIVATE ${TEXTURE_TEST_SOURCES})

target_include_directories(Texture_Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Source")

target_compile_definitions(Texture_Tests PRIVATE
    JUCE_UNIT_TESTS=1
    TEXTURE_ENABLE_LICENSE_ISSUER=1
    TEXTURE_LICENSE_SECRET="TEXTURE-TEST-SECRET"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(Texture_Tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

texture_apply_strict_warnings(Texture_Tests ${TEXTURE_TEST_SOURCES})

# The unit tests and the fuzzer are separate CTest entries: the fuzzer is
# wall-clock bounded (17_TESTING.md) and a failure there means something quite
# different from a unit test failure.
add_test(NAME texture_unit_tests COMMAND Texture_Tests texture)
add_test(NAME texture_fuzz COMMAND Texture_Tests fuzz)

set_tests_properties(texture_unit_tests PROPERTIES TIMEOUT 600)
set_tests_properties(texture_fuzz PROPERTIES TIMEOUT 300)

# Pure waveform math also runs without JUCE for focused boundary/sanitizer checks.
add_executable(Texture_WaveformMathTests test_waveform_math.cpp)
texture_apply_strict_warnings(Texture_WaveformMathTests test_waveform_math.cpp)
add_test(NAME texture_waveform_math COMMAND Texture_WaveformMathTests)
