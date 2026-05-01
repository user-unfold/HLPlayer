# ============================================================================
# HLPlayer Stream Extractor
# ============================================================================
# This module provides URL stream extraction functionality.
# IMPORTANT: This module is isolated from the core SDK to avoid bundling
# Python runtime (yt-dlp) in the core SDK.
# ============================================================================

# Find FFmpeg package
set(LOCAL_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1")

if(EXISTS "${LOCAL_FFMPEG_DIR}")
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIRS "${LOCAL_FFMPEG_DIR}/include")
    set(FFMPEG_LIBRARY_DIRS "${LOCAL_FFMPEG_DIR}/lib")
    # For MinGW or standard libs
    set(FFMPEG_LIBRARIES avformat avcodec avutil swscale swresample avfilter)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET
            libavformat
            libavcodec
            libavutil
            libswscale
            libswresample
            libavfilter
        )
    endif()
endif()

    if(FFMPEG_FOUND)
    set(EXTRACTOR_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegDemuxer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegVideoDecoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegAudioDecoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegMuxer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/StreamExtractor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ExtractorFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/HWDevicePool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/DecoderSelector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/HWVideoEncoder.cpp
    )
else()
    set(EXTRACTOR_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/StreamExtractor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ExtractorFactory.cpp
    )
endif()

set(EXTRACTOR_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegRAII.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegDemuxer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegVideoDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegAudioDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FFmpegMuxer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/StreamExtractor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ExtractorFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HWDevicePool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DecoderSelector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HWVideoEncoder.h
)

# Create the extractor library
add_library(hlplayer_extractor SHARED
    ${EXTRACTOR_SOURCES}
    ${EXTRACTOR_HEADERS}
)

# Set library properties
set_target_properties(hlplayer_extractor PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Include directories
target_include_directories(hlplayer_extractor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${FFMPEG_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(hlplayer_extractor
    PUBLIC
        hlplayer_core
        nlohmann_json::nlohmann_json
    PRIVATE
        ${FFMPEG_LIBRARIES}
)

if(FFMPEG_FOUND)
    target_link_directories(hlplayer_extractor PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_extractor PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_extractor PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_extractor PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_extractor PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(hlplayer_extractor PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Export symbols (for Windows DLL)
if(WIN32)
    target_compile_definitions(hlplayer_extractor PRIVATE
        HLPLAYER_EXTRACTOR_EXPORTS
    )
endif()

if(FFMPEG_FOUND)
    target_compile_definitions(hlplayer_extractor PRIVATE HAVE_FFMPEG)
endif()

# Link D3D11/DXGI on Windows (used by HWDevicePool and FFmpegVideoDecoder)
if(WIN32)
    target_link_libraries(hlplayer_extractor PRIVATE d3d11 dxgi)
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS hlplayer_extractor
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer/extractor
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# install(EXPORT ...) - Exporting targets with FetchContent dependencies requires
# additional configuration. Disabled for scaffolding phase.
