# Reverie - standalone, engine-agnostic audio middleware.
# Copyright (c) Hollow Dream Studios. All rights reserved.
#
# This project builds independently of Heartbreak/Unity/Unreal. Engines consume ReverieRuntime
# (and the C ABI in SDK/CAPI/reverie.h); Reverie depends on none of them.
cmake_minimum_required(VERSION 3.20)
project(Reverie VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime (/MT), matching the HDS ecosystem (Heartbreak + HDS Resonance) so Reverie
# can later be linked into Heartbreak without a CRT mismatch. Standalone builds are unaffected.
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# ReverieRuntime - the engine-agnostic runtime + SDK, as one static library.
# ---------------------------------------------------------------------------
set(REVERIE_SOURCES
    Runtime/Core/Log.cpp
    Runtime/Audio/AudioFormat.cpp
    Runtime/Audio/AudioDecoder.cpp
    Runtime/Audio/AudioEngine.cpp
    Runtime/Voices/VoiceManager.cpp
    Runtime/Mixer/Mixer.cpp
    Runtime/Spatial/PanningSpatialRenderer.cpp
    Runtime/Spatial/ResonanceSpatialRenderer.cpp
    Runtime/Events/EventSystem.cpp
    Runtime/Platform/AudioDeviceManager.cpp
    Runtime/Platform/NullDevice.cpp
    Runtime/Platform/MiniaudioDevice.cpp
    Runtime/Platform/MiniaudioImpl.cpp
    SDK/Reverie.cpp
    SDK/CAPI/reverie_c.cpp)

add_library(ReverieRuntime STATIC ${REVERIE_SOURCES})
target_compile_features(ReverieRuntime PUBLIC cxx_std_17)

# Public: Runtime (Core/Types.h + subsystem headers used by the SDK facade) + the SDK headers.
# Private: the vendored miniaudio single header - never part of the public surface.
target_include_directories(ReverieRuntime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/CAPI")
target_include_directories(ReverieRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/miniaudio")

if(MSVC)
    target_compile_options(ReverieRuntime PRIVATE /W4)
    target_compile_definitions(ReverieRuntime PRIVATE _CRT_SECURE_NO_WARNINGS)
    # miniaudio is a large third-party single header; do not hold it to Reverie's warning bar.
    set_source_files_properties(
        Runtime/Platform/MiniaudioImpl.cpp
        Runtime/Platform/MiniaudioDevice.cpp
        Runtime/Audio/AudioDecoder.cpp
        Runtime/Spatial/ResonanceSpatialRenderer.cpp # includes the vraudio (third-party) header
        PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(ReverieRuntime PRIVATE -Wall -Wextra)
endif()

# Threads for the mixer lock + miniaudio's audio thread. On *nix miniaudio also wants dl + m.
find_package(Threads REQUIRED)
target_link_libraries(ReverieRuntime PRIVATE Threads::Threads)
if(UNIX AND NOT APPLE)
    target_link_libraries(ReverieRuntime PRIVATE ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------------------
# Optional HDS Resonance HRTF spatial backend (the fork is a SEPARATE sibling repo).
# ---------------------------------------------------------------------------
# OFF by default so a standalone Reverie build needs none of the fork. When ON (and the fork +
# its fetched deps are present at ../HDSResonance-Audio), the fork's static API lib is linked and
# Spatial/ResonanceSpatialRenderer.cpp is compiled with REVERIE_WITH_RESONANCE=1. When OFF that
# adapter compiles to a stub (its factory returns nullptr) and the engine uses panning. Reverie
# depends on the fork ONLY here; no Reverie source but the adapter includes vraudio.
option(REVERIE_WITH_RESONANCE "Build the HDS Resonance HRTF spatial backend" OFF)
set(REVERIE_RESONANCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../HDSResonance-Audio"
    CACHE PATH "Path to the HDS Resonance Audio fork checkout")
if(REVERIE_WITH_RESONANCE AND EXISTS "${REVERIE_RESONANCE_DIR}/CMakeLists.txt"
   AND EXISTS "${REVERIE_RESONANCE_DIR}/third_party/eigen/Eigen/Core")
    set(BUILD_RESONANCE_AUDIO_API   ON  CACHE BOOL "" FORCE)
    set(BUILD_RESONANCE_AUDIO_TESTS OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # the fork declares an ancient cmake_minimum_required
    add_subdirectory("${REVERIE_RESONANCE_DIR}" "${CMAKE_BINARY_DIR}/hdsresonance" EXCLUDE_FROM_ALL)
    target_link_libraries(ReverieRuntime PRIVATE ResonanceAudioStatic)
    target_include_directories(ReverieRuntime PRIVATE "${REVERIE_RESONANCE_DIR}")
    target_compile_definitions(ReverieRuntime PRIVATE REVERIE_WITH_RESONANCE=1)
    message(STATUS "Reverie: HDS Resonance HRTF spatial backend ENABLED (${REVERIE_RESONANCE_DIR}).")
else()
    target_compile_definitions(ReverieRuntime PRIVATE REVERIE_WITH_RESONANCE=0)
    if(REVERIE_WITH_RESONANCE)
        message(WARNING "Reverie: Resonance requested but fork/deps missing at "
                        "${REVERIE_RESONANCE_DIR}; building with panning only.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (headless, deterministic - no audio device required).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(ReverieTests Tests/CoreTests.cpp)
target_link_libraries(ReverieTests PRIVATE ReverieRuntime)
add_test(NAME reverie_core COMMAND ReverieTests)

add_executable(ReverieEventTests Tests/EventTests.cpp)
target_link_libraries(ReverieEventTests PRIVATE ReverieRuntime)
add_test(NAME reverie_events COMMAND ReverieEventTests)

add_executable(ReverieMixerTests Tests/MixerTests.cpp)
target_link_libraries(ReverieMixerTests PRIVATE ReverieRuntime)
add_test(NAME reverie_mixer COMMAND ReverieMixerTests)

add_executable(ReverieSpatialTests Tests/SpatialTests.cpp)
target_link_libraries(ReverieSpatialTests PRIVATE ReverieRuntime)
add_test(NAME reverie_spatial COMMAND ReverieSpatialTests)

# Verifies the Resonance HRTF backend when built with REVERIE_WITH_RESONANCE; skips (passes)
# when Reverie was built with panning only. Detects the active backend at runtime.
add_executable(ReverieResonanceTests Tests/ResonanceTests.cpp)
target_link_libraries(ReverieResonanceTests PRIVATE ReverieRuntime)
add_test(NAME reverie_resonance COMMAND ReverieResonanceTests)
