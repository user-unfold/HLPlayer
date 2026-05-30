# ============================================================================
# HLPlayer Camera Recording + Streaming Module
# ============================================================================
# This module provides camera capture, recording, and streaming functionality.
# Uses FFmpeg for device access (avdevice), encoding, and muxing.
# ============================================================================

# Find FFmpeg package
set(LOCAL_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1")

if(EXISTS "${LOCAL_FFMPEG_DIR}")
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIRS "${LOCAL_FFMPEG_DIR}/include")
    set(FFMPEG_LIBRARY_DIRS "${LOCAL_FFMPEG_DIR}/lib")
    # For MinGW or standard libs (include avdevice for camera capture)
    set(FFMPEG_LIBRARIES avformat avcodec avutil avdevice swscale swresample)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET
            libavformat
            libavcodec
            libavutil
            libavdevice
            libswscale
            libswresample
        )
    endif()
endif()

if(FFMPEG_FOUND)
    set(CAMERA_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/CameraSource.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/AudioEncoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/StreamMuxer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/RecordingPipeline.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/RecordingPipelineV2.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/StreamingPipeline.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/PreviewRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/HWEncoderDetector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/HWVideoEncoder.cpp
    )
else()
    set(CAMERA_SOURCES)
endif()

# QMLCameraRecorder.cpp is compiled in hlplayer_qml (not here) to avoid
# duplicate symbols — QML_ELEMENT registration requires it in the same
# target that calls qt6_add_qml_module.

set(CAMERA_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/CameraSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/AudioEncoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/StreamMuxer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/RecordingPipeline.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/RecordingPipelineV2.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/StreamingPipeline.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/PreviewRenderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/CameraTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/HWEncoderDetector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hlplayer/HWVideoEncoder.h
)

# QMLCameraRecorder.h is compiled in hlplayer_qml, not here.

# Create the camera library
add_library(hlplayer_camera SHARED
    ${CAMERA_SOURCES}
    ${CAMERA_HEADERS}
)

# Set library properties
set_target_properties(hlplayer_camera PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

# Include directories
target_include_directories(hlplayer_camera
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${FFMPEG_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/extractor
)

# Link dependencies
target_link_libraries(hlplayer_camera
    PUBLIC
        hlplayer_core
        nlohmann_json::nlohmann_json
    PRIVATE
        ${FFMPEG_LIBRARIES}
)

if(FFMPEG_FOUND)
    target_link_directories(hlplayer_camera PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(hlplayer_camera PRIVATE
        ${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1/lib/libswscale.dll.a
    )
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_camera PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_camera PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_camera PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_camera PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(hlplayer_camera PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Export symbols (for Windows DLL)
if(WIN32)
    target_compile_definitions(hlplayer_camera PRIVATE
        HLPLAYER_CAMERA_EXPORTS
    )
endif()

if(FFMPEG_FOUND)
    target_compile_definitions(hlplayer_camera PRIVATE HAVE_FFMPEG)
endif()

# Qt6/QML integration for preview rendering
if(BUILD_QML)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)

    target_include_directories(hlplayer_camera PRIVATE ${Qt6Core_INCLUDE_DIRS})
    target_link_libraries(hlplayer_camera PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
    target_compile_definitions(hlplayer_camera PRIVATE BUILD_QML)
endif()

# HLPLAYER_CAMERA_QML_EXPORTS is now defined in hlplayer_qml
# since QMLCameraRecorder.cpp is compiled there.

# Installation rules
include(GNUInstallDirs)

install(TARGETS hlplayer_camera
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Tests
add_subdirectory(tests)
