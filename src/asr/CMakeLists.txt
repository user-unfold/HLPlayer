# ============================================================================
# HLPlayer ASR Module (Real-time Speech Recognition + Subtitles)
# ============================================================================
# This module provides real-time automatic speech recognition using Whisper.cpp
# with CUDA GPU acceleration (CPU fallback), audio capture from multiple sources,
# and SRT subtitle export.
# ============================================================================

# ----------------------------------------------------------------------------
# Find Qt6 (for QML bridge)
# ----------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Qml)

# ----------------------------------------------------------------------------
# Whisper.cpp integration (git submodule at 3rdparty/whisper.cpp)
# ----------------------------------------------------------------------------
set(WHISPER_DIR "${CMAKE_SOURCE_DIR}/3rdparty/whisper.cpp")

if(NOT EXISTS "${WHISPER_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "whisper.cpp not found at ${WHISPER_DIR}\n"
        "Please initialize the git submodule:\n"
        "  git submodule update --init 3rdparty/whisper.cpp")
endif()

# Configure whisper.cpp build options
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

# GPU acceleration: Vulkan (cross-platform GPU backend for whisper.cpp)
set(GGML_VULKAN ON CACHE BOOL "Enable Vulkan GPU backend" FORCE)
set(GGML_CUDA OFF CACHE BOOL "Disable CUDA (needs MSVC)" FORCE)

# Save CMake state before whisper.cpp (it may modify globals)
set(_ASR_SAVE_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
set(_ASR_SAVE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")

add_subdirectory("${WHISPER_DIR}" "${CMAKE_BINARY_DIR}/whisper.cpp" EXCLUDE_FROM_ALL)

# Restore CMake state
set(CMAKE_BUILD_TYPE "${_ASR_SAVE_BUILD_TYPE}" CACHE STRING "Build type" FORCE)
set(CMAKE_CXX_STANDARD "${_ASR_SAVE_CXX_STANDARD}")

# ----------------------------------------------------------------------------
# FFmpeg (for audio resampling via libswresample)
# ----------------------------------------------------------------------------
set(LOCAL_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1")

if(EXISTS "${LOCAL_FFMPEG_DIR}")
    set(ASR_FFMPEG_FOUND TRUE)
    set(ASR_FFMPEG_INCLUDE_DIRS "${LOCAL_FFMPEG_DIR}/include")
    set(ASR_FFMPEG_LIBRARY_DIRS "${LOCAL_FFMPEG_DIR}/lib")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ASR_FFMPEG QUIET libswresample libavutil)
    endif()
endif()

if(NOT ASR_FFMPEG_FOUND)
    message(FATAL_ERROR "ASR module requires FFmpeg (libswresample). Not found.")
endif()

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
set(ASR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WhisperEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AudioResampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SubtitleManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ASRPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ASRSettings.cpp
)

if(WIN32)
    list(APPEND ASR_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/WASAPICapture.cpp
    )
endif()

set(ASR_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/ASRExport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/ASRTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/IASREngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/IAudioCapture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/WhisperEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/AudioResampler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/SubtitleManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/ASRPipeline.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/ASRSettings.h
)

if(WIN32)
    list(APPEND ASR_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/WASAPICapture.h
    )
endif()

# ----------------------------------------------------------------------------
# Library target
# ----------------------------------------------------------------------------
add_library(hlplayer_asr SHARED
    ${ASR_SOURCES}
    ${ASR_HEADERS}
)

set_target_properties(hlplayer_asr PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

# ----------------------------------------------------------------------------
# Include directories
# ----------------------------------------------------------------------------
target_include_directories(hlplayer_asr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ASR_FFMPEG_INCLUDE_DIRS}
        ${WHISPER_DIR}/include
        ${WHISPER_DIR}/ggml/include
)

# ----------------------------------------------------------------------------
# Link dependencies
# ----------------------------------------------------------------------------
target_link_libraries(hlplayer_asr
    PRIVATE
        hlplayer_core
        whisper
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        concurrentqueue
        Qt6::Core
        Qt6::Qml
)

# FFmpeg libswresample + libavutil for audio resampling
if(ASR_FFMPEG_FOUND AND EXISTS "${ASR_FFMPEG_LIBRARY_DIRS}")
    target_link_directories(hlplayer_asr PRIVATE ${ASR_FFMPEG_LIBRARY_DIRS})
endif()
target_link_libraries(hlplayer_asr PRIVATE swresample avutil)

# WASAPI audio capture (Windows)
if(WIN32)
    target_link_libraries(hlplayer_asr PRIVATE
        ole32
        ksuser
        propsys
    )
endif()

# ----------------------------------------------------------------------------
# Compile definitions
# ----------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(hlplayer_asr PRIVATE
        HLPLAYER_ASR_EXPORTS
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_asr PRIVATE __APPLE__)
elseif(UNIX)
    target_compile_definitions(hlplayer_asr PRIVATE _LINUX)
endif()

if(CUDAToolkit_FOUND)
    target_compile_definitions(hlplayer_asr PRIVATE GGML_USE_CUDA)
endif()

# ----------------------------------------------------------------------------
# Compiler warnings
# ----------------------------------------------------------------------------
if(MSVC)
    target_compile_options(hlplayer_asr PRIVATE /W4)
else()
    target_compile_options(hlplayer_asr PRIVATE
        -Wall
        -Wextra
        -Wno-error
    )
endif()

# ----------------------------------------------------------------------------
# Installation
# ----------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS hlplayer_asr
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ----------------------------------------------------------------------------
# QML files
# ----------------------------------------------------------------------------
if(BUILD_ASR)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/qml/
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/hlplayer/qml
        FILES_MATCHING PATTERN "*.qml"
    )
endif()
