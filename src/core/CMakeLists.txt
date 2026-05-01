# ============================================================================
# HLPlayer Core Library
# ============================================================================
# This is the core SDK library that provides the streaming player functionality.
# It is designed to be embedded in applications via C++ API.
# ============================================================================

# Collect source files
file(GLOB_RECURSE CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

# Create the core library
add_library(hlplayer_core SHARED
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

# Set library version
set_target_properties(hlplayer_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Include directories
target_include_directories(hlplayer_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../render
)

# Optional FFmpeg headers (for codec ID mapping and avfilter)
set(LOCAL_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1")
if(EXISTS "${LOCAL_FFMPEG_DIR}/include")
    target_include_directories(hlplayer_core PRIVATE "${LOCAL_FFMPEG_DIR}/include")
endif()
if(EXISTS "${LOCAL_FFMPEG_DIR}/lib")
    target_link_directories(hlplayer_core PRIVATE "${LOCAL_FFMPEG_DIR}/lib")
endif()


# Link dependencies
target_link_libraries(hlplayer_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        concurrentqueue
    PRIVATE
        avformat
        avcodec
        avfilter
        avutil
        swscale
        swresample
)

# Link DXGI on Windows for VRAM queries
if(WIN32)
    target_link_libraries(hlplayer_core PRIVATE dxgi)
endif()

# Compile-time options
if(TELEMETRY_ENABLED)
    target_compile_definitions(hlplayer_core PRIVATE TELEMETRY_ENABLED)
endif()

if(TRACE_ENABLED)
    target_compile_definitions(hlplayer_core PRIVATE TRACE_ENABLED)
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_core PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_core PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_core PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_core PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(hlplayer_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Export symbols (for Windows DLL)
if(WIN32)
    target_compile_definitions(hlplayer_core PRIVATE
        HLPLAYER_CORE_EXPORTS
    )
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS hlplayer_core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer/core
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# install(EXPORT ...) - Exporting targets with FetchContent dependencies requires
# additional configuration. Disabled for scaffolding phase.
