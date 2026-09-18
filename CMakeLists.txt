cmake_minimum_required(VERSION 3.22)

project(Texture VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Universal binary" FORCE)
    endif()
endif()

# --- Options ------------------------------------------------------------------
option(TEXTURE_BUILD_TESTS  "Build the unit test runner"                    ON)
option(TEXTURE_BUILD_CLAP   "Also build a CLAP (fetches clap-juce-extensions)" OFF)
option(TEXTURE_ENABLE_ASAN  "Enable AddressSanitizer and UBSan (never ship)" OFF)
option(TEXTURE_WARNINGS_AS_ERRORS "Treat warnings in our own code as errors" ON)

set(TEXTURE_CODESIGN_IDENTITY "" CACHE STRING "Developer ID used for release builds on macOS")
set(TEXTURE_AAX_SDK_PATH "" CACHE PATH "Path to the AAX SDK; enables the AAX target when set")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/JuceFetch.cmake)

# --- Placeholder factory samples ----------------------------------------------
# Built first so a fresh clone produces an audible plugin with no manual steps
# (18_BUILD_AND_PACKAGING.md).
add_executable(generate_placeholder_samples
    tools/generate_placeholder_samples.cpp
    Source/IO/FactoryContent.cpp
    Source/IO/FactoryContent.h)

target_link_libraries(generate_placeholder_samples
    PRIVATE
        juce::juce_audio_formats
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_compile_definitions(generate_placeholder_samples
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)

set(TEXTURE_FACTORY_SAMPLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/factory_samples")
set(TEXTURE_SAMPLE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/factory_samples.stamp")

add_custom_command(
    OUTPUT "${TEXTURE_SAMPLE_STAMP}"
    COMMAND generate_placeholder_samples "${TEXTURE_FACTORY_SAMPLES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${TEXTURE_SAMPLE_STAMP}"
    DEPENDS generate_placeholder_samples
    COMMENT "TEXTURE: generating the 20 placeholder factory samples"
    VERBATIM)

add_custom_target(texture_factory_samples DEPENDS "${TEXTURE_SAMPLE_STAMP}")

# --- Plugin -------------------------------------------------------------------
set(TEXTURE_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND TEXTURE_FORMATS AU)
endif()

if(TEXTURE_AAX_SDK_PATH)
    juce_set_aax_sdk_path("${TEXTURE_AAX_SDK_PATH}")
    list(APPEND TEXTURE_FORMATS AAX)
    message(STATUS "TEXTURE: AAX enabled using ${TEXTURE_AAX_SDK_PATH}")
endif()

juce_add_plugin(Texture
    COMPANY_NAME                TextureAudio
    BUNDLE_ID                   com.textureaudio.texture
    PLUGIN_MANUFACTURER_CODE    TxAu
    PLUGIN_CODE                 Txtr
    FORMATS                     ${TEXTURE_FORMATS}
    PRODUCT_NAME                Texture
    DESCRIPTION                 "Drag-and-drop granular pad synthesizer"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    AAX_CATEGORY                AAX_ePlugInCategory_SWGenerators
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Images/logo.png"
    ICON_SMALL                  "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Images/logo.png")

add_dependencies(Texture texture_factory_samples)

# --- Sources ------------------------------------------------------------------
set(TEXTURE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h

    Source/Engine/GranularEngine.cpp
    Source/Engine/GranularEngine.h
    Source/Engine/Grain.cpp
    Source/Engine/Grain.h
    Source/Engine/SampleBuffer.cpp
    Source/Engine/SampleBuffer.h
    Source/Engine/Voice.cpp
    Source/Engine/Voice.h

    Source/Effects/Chorus.cpp
    Source/Effects/Chorus.h
    Source/Effects/EffectsChain.cpp
    Source/Effects/EffectsChain.h
    Source/Effects/Reverb.cpp
    Source/Effects/Reverb.h
    Source/Effects/Saturator.cpp
    Source/Effects/Saturator.h
    Source/Effects/SecretEffect.cpp
    Source/Effects/SecretEffect.h

    Source/Modulation/EnvelopeADSR.cpp
    Source/Modulation/EnvelopeADSR.h
    Source/Modulation/MorphLFO.cpp
    Source/Modulation/MorphLFO.h

    Source/IO/FactoryContent.cpp
    Source/IO/FactoryContent.h
    Source/IO/SampleLoader.cpp
    Source/IO/SampleLoader.h
    Source/IO/WavExporter.cpp
    Source/IO/WavExporter.h

    Source/Presets/PresetManager.cpp
    Source/Presets/PresetManager.h
    Source/Presets/PresetBrowser.cpp
    Source/Presets/PresetBrowser.h

    Source/License/LicenseManager.cpp
    Source/License/LicenseManager.h

    Source/GUI/Colors.h
    Source/GUI/DebugWindow.cpp
    Source/GUI/DebugWindow.h
    Source/GUI/DropZone.cpp
    Source/GUI/DropZone.h
    Source/GUI/HelpPanel.cpp
    Source/GUI/HelpPanel.h
    Source/GUI/LookAndFeel.cpp
    Source/GUI/LookAndFeel.h
    Source/GUI/MainPanel.cpp
    Source/GUI/MainPanel.h
    Source/GUI/MorphKnob.cpp
    Source/GUI/MorphKnob.h
    Source/GUI/SecretPanel.cpp
    Source/GUI/SecretPanel.h
    Source/GUI/SettingsPanel.cpp
    Source/GUI/SettingsPanel.h
    Source/GUI/WaveformDisplay.cpp
    Source/GUI/WaveformDisplay.h
    Source/GUI/Widgets.cpp
    Source/GUI/Widgets.h

    Source/Utility/AtomicHelpers.h
    Source/Utility/DspHelpers.h
    Source/Utility/Logging.cpp
    Source/Utility/Logging.h
    Source/Utility/MidiLearn.cpp
    Source/Utility/MidiLearn.h
    Source/Utility/ParameterIDs.cpp
    Source/Utility/ParameterIDs.h)

target_sources(Texture PRIVATE ${TEXTURE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX Source FILES ${TEXTURE_SOURCES})

target_include_directories(Texture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

# --- Binary data ---------------------------------------------------------------
# Fonts and the logo are optional: the look and feel falls back to the system sans
# if they are missing (22_FILE_MANIFEST.md, "Fonts note").
set(TEXTURE_BINARY_ASSETS "")

foreach(asset
        "Resources/Fonts/Inter-Regular.ttf"
        "Resources/Fonts/Inter-Bold.ttf"
        "Resources/Images/logo.png")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
        list(APPEND TEXTURE_BINARY_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
    else()
        message(STATUS "TEXTURE: optional asset missing, skipping: ${asset}")
    endif()
endforeach()

if(TEXTURE_BINARY_ASSETS)
    juce_add_binary_data(TextureBinaryData SOURCES ${TEXTURE_BINARY_ASSETS})
    set_target_properties(TextureBinaryData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_link_libraries(Texture PRIVATE TextureBinaryData)
endif()

# --- Definitions and libraries --------------------------------------------------
target_compile_definitions(Texture
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(Texture
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Warnings -------------------------------------------------------------------
# Strict on our own code; JUCE emits warnings we do not own (18_BUILD_AND_PACKAGING.md).
function(texture_apply_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)

        if(TEXTURE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)

        if(TEXTURE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

texture_apply_strict_warnings(Texture)

# --- Sanitizers ------------------------------------------------------------------
if(TEXTURE_ENABLE_ASAN)
    if(MSVC)
        message(WARNING "TEXTURE: ASan is only wired up for clang and gcc; ignoring.")
    else()
        target_compile_options(Texture PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(Texture PRIVATE -fsanitize=address,undefined)
    endif()
endif()

# --- macOS signing ----------------------------------------------------------------
if(APPLE AND TEXTURE_CODESIGN_IDENTITY)
    foreach(format ${TEXTURE_FORMATS})
        if(TARGET Texture_${format})
            set_target_properties(Texture_${format} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${TEXTURE_CODESIGN_IDENTITY}"
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual")
        endif()
    endforeach()
endif()

# --- CLAP --------------------------------------------------------------------------
if(TEXTURE_BUILD_CLAP)
    include(FetchContent)

    FetchContent_Declare(
        clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE)

    FetchContent_MakeAvailable(clap-juce-extensions)

    clap_juce_extensions_plugin(
        TARGET Texture
        CLAP_ID com.textureaudio.texture
        CLAP_FEATURES instrument synthesizer sampler)

    message(STATUS "TEXTURE: CLAP target enabled")
endif()

# --- Tests ---------------------------------------------------------------------------
if(TEXTURE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "TEXTURE ${PROJECT_VERSION}: formats = ${TEXTURE_FORMATS}, tests = ${TEXTURE_BUILD_TESTS}")
