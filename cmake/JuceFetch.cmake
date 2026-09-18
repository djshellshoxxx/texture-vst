# Locates JUCE.
#
# Reproducibility matters more than convenience here (18_BUILD_AND_PACKAGING.md),
# so the version is pinned to an exact tag. A local checkout can be supplied with
# -DTEXTURE_JUCE_PATH=<dir> to keep CI and offline builds off the network.

set(TEXTURE_JUCE_TAG "8.0.8" CACHE STRING "JUCE tag to build against")
set(TEXTURE_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (skips the download)")

if(TEXTURE_JUCE_PATH AND EXISTS "${TEXTURE_JUCE_PATH}/CMakeLists.txt")
    message(STATUS "TEXTURE: using local JUCE at ${TEXTURE_JUCE_PATH}")
    add_subdirectory("${TEXTURE_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)

    message(STATUS "TEXTURE: fetching JUCE ${TEXTURE_JUCE_TAG}")

    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${TEXTURE_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )

    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT TARGET juce::juce_audio_processors)
    message(FATAL_ERROR "TEXTURE: JUCE was not found. Set -DTEXTURE_JUCE_PATH to a checkout.")
endif()
