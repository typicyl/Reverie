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
    Runtime/Audio/StreamManager.cpp
    Runtime/Voices/VoiceManager.cpp
    Runtime/Mixer/Mixer.cpp
    Runtime/Parameters/ParameterStore.cpp
    Runtime/DSP/BiquadFilter.cpp
    Runtime/DSP/DynamicsEffect.cpp
    Runtime/DSP/DelayEffect.cpp
    Runtime/Music/MusicSystem.cpp
    Runtime/Serialization/Bank.cpp
    Runtime/Serialization/Hdsrf.cpp
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

# Public surface is ONLY the SDK: <Reverie/Reverie.h> (which resolves "Core/Types.h" relative to
# itself) and the flat C ABI <reverie.h>. Runtime/ is PRIVATE, so a consumer cannot reach internal
# headers (Mixer/VoiceManager/AudioEngine/...) and shipping the SDK means shipping SDK/ only.
target_include_directories(ReverieRuntime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/CAPI")
target_include_directories(ReverieRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime"
    # Internal code says #include "Core/Types.h"; the public vocabulary now lives under the SDK, so
    # add SDK/Include/Reverie to the PRIVATE search path to resolve it for the runtime TUs.
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Include/Reverie"
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
# Tools: reverie-cook - offline asset cooker (source audio -> .HDSRF). Uses internal runtime
# headers (the decoder + codec), so it gets the private include paths.
# ---------------------------------------------------------------------------
add_executable(reverie-cook Tools/Cook/main.cpp)
target_link_libraries(reverie-cook PRIVATE ReverieRuntime)
target_include_directories(reverie-cook PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Include/Reverie")

# ---------------------------------------------------------------------------
# Reverie Studio - the authoring application core (project model + project->runtime build/preview).
# A proper SDK consumer (no runtime internals). The Dear ImGui window is a thin view over this and
# is added when a UI stack is present; the core here is fully headless-testable.
# ---------------------------------------------------------------------------
add_library(ReverieStudioLib STATIC Studio/StudioProject.cpp Studio/StudioApp.cpp)
target_link_libraries(ReverieStudioLib PUBLIC ReverieRuntime)
target_include_directories(ReverieStudioLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Studio")

add_executable(reverie-studio Studio/main.cpp)
target_link_libraries(reverie-studio PRIVATE ReverieStudioLib)

# ---------------------------------------------------------------------------
# Reverie Studio GUI (Dear ImGui editor). Win32 + DX11 is the initial concrete backend, but the
# editor talks to REPLACEABLE platform/renderer interfaces (Studio/App/StudioBackend.h) - it never
# includes Win32/DX11 or even ImGui backend headers. Only the backend TU knows the platform. The
# runtime, SDK, model, cooker and .HDSRF stay completely ImGui-free.
# ---------------------------------------------------------------------------
option(REVERIE_BUILD_STUDIO_GUI "Build the Dear ImGui Reverie Studio editor" ${WIN32})
if(REVERIE_BUILD_STUDIO_GUI AND WIN32)
    include(FetchContent)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui) # imgui has no CMakeLists; this just populates imgui_SOURCE_DIR

    # Dear ImGui core + the Win32 platform backend + the DX11 renderer backend, as one static lib.
    add_library(reverie_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    target_include_directories(reverie_imgui PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
    target_compile_definitions(reverie_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    if(MSVC)
        target_compile_options(reverie_imgui PRIVATE /W0) # third-party
    endif()

    add_executable(reverie-studio-gui
        Studio/App/gui_main.cpp
        Studio/App/EditorApp.cpp
        Studio/App/Panels.cpp
        Studio/App/Theme.cpp
        Studio/App/Win32Dx11Backend.cpp)
    target_link_libraries(reverie-studio-gui PRIVATE
        ReverieStudioLib reverie_imgui d3d11 dxgi d3dcompiler)
    target_include_directories(reverie-studio-gui PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Studio"
        "${CMAKE_CURRENT_SOURCE_DIR}/Studio/App")
    message(STATUS "Reverie: Studio GUI editor ENABLED (Dear ImGui, Win32+DX11 backend).")
endif()

# ---------------------------------------------------------------------------
# Tests (headless, deterministic - no audio device required).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(ReverieTests Tests/CoreTests.cpp)
target_link_libraries(ReverieTests PRIVATE ReverieRuntime)
add_test(NAME reverie_core COMMAND ReverieTests)

# Lock-free SPSC ring - the substrate of the real-time control<->audio boundary (Phase 1). This
# test reaches into an internal header (Core/SpscRing.h), so it needs the PRIVATE runtime paths
# that ReverieRuntime does not export to ordinary consumers.
add_executable(ReverieRingTests Tests/RingTests.cpp)
target_link_libraries(ReverieRingTests PRIVATE ReverieRuntime Threads::Threads)
target_include_directories(ReverieRingTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Include/Reverie")
add_test(NAME reverie_ring COMMAND ReverieRingTests)

# Two-thread stress of the lock-free voice pool (control ops vs the render pull). Uses only the SDK.
add_executable(ReverieConcurrencyTests Tests/ConcurrencyTests.cpp)
target_link_libraries(ReverieConcurrencyTests PRIVATE ReverieRuntime Threads::Threads)
add_test(NAME reverie_concurrency COMMAND ReverieConcurrencyTests)

add_executable(ReverieEventTests Tests/EventTests.cpp)
target_link_libraries(ReverieEventTests PRIVATE ReverieRuntime)
add_test(NAME reverie_events COMMAND ReverieEventTests)

add_executable(ReverieMixerTests Tests/MixerTests.cpp)
target_link_libraries(ReverieMixerTests PRIVATE ReverieRuntime)
add_test(NAME reverie_mixer COMMAND ReverieMixerTests)

add_executable(ReverieParamTests Tests/ParamTests.cpp)
target_link_libraries(ReverieParamTests PRIVATE ReverieRuntime)
add_test(NAME reverie_params COMMAND ReverieParamTests)

add_executable(ReverieDspTests Tests/DspTests.cpp)
target_link_libraries(ReverieDspTests PRIVATE ReverieRuntime)
add_test(NAME reverie_dsp COMMAND ReverieDspTests)

add_executable(ReverieBankTests Tests/BankTests.cpp)
target_link_libraries(ReverieBankTests PRIVATE ReverieRuntime)
add_test(NAME reverie_bank COMMAND ReverieBankTests)

add_executable(ReverieMusicTests Tests/MusicTests.cpp)
target_link_libraries(ReverieMusicTests PRIVATE ReverieRuntime)
add_test(NAME reverie_music COMMAND ReverieMusicTests)

# Exercises the .HDSRF codec at the runtime level, so it needs the internal headers.
add_executable(ReverieHdsrfTests Tests/HdsrfTests.cpp)
target_link_libraries(ReverieHdsrfTests PRIVATE ReverieRuntime)
target_include_directories(ReverieHdsrfTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Include/Reverie")
add_test(NAME reverie_hdsrf COMMAND ReverieHdsrfTests)

# The cooker's in-memory cook+verify path (runnable in CI without an input file).
add_test(NAME reverie_cook_selftest COMMAND reverie-cook --selftest)

# Reverie Studio: authoring-core tests + the app's own self-test.
add_executable(ReverieStudioTests Tests/StudioTests.cpp)
target_link_libraries(ReverieStudioTests PRIVATE ReverieStudioLib)
add_test(NAME reverie_studio COMMAND ReverieStudioTests)
add_test(NAME reverie_studio_selftest COMMAND reverie-studio --selftest)

add_executable(ReverieSpatialTests Tests/SpatialTests.cpp)
target_link_libraries(ReverieSpatialTests PRIVATE ReverieRuntime)
add_test(NAME reverie_spatial COMMAND ReverieSpatialTests)

# Verifies the Resonance HRTF backend when built with REVERIE_WITH_RESONANCE; skips (passes)
# when Reverie was built with panning only. Detects the active backend at runtime.
add_executable(ReverieResonanceTests Tests/ResonanceTests.cpp)
target_link_libraries(ReverieResonanceTests PRIVATE ReverieRuntime)
add_test(NAME reverie_resonance COMMAND ReverieResonanceTests)
