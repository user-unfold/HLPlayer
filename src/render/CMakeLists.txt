# ============================================================================
# HLPlayer Render Bridge
# ============================================================================
# This module provides the rendering bridge between the media engine and UI.
# It handles zero-copy GPU frame delivery to rendering backends.
# ============================================================================

# Find Qt6 Multimedia for audio rendering
find_package(Qt6 REQUIRED COMPONENTS Multimedia)

set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../SDL2-2.30.8/x86_64-w64-mingw32/lib/cmake/SDL2")
find_package(SDL2 REQUIRED)

# Collect source files
file(GLOB_RECURSE RENDER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE RENDER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

# Create the render library
add_library(hlplayer_render SHARED
    ${RENDER_SOURCES}
    ${RENDER_HEADERS}
)

# Set library properties
set_target_properties(hlplayer_render PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

# Include directories
target_include_directories(hlplayer_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(hlplayer_render
    PUBLIC
        hlplayer_core
        Qt6::Multimedia
        SDL2::SDL2
    PRIVATE
        spdlog::spdlog
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_render PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_render PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_render PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_render PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(hlplayer_render PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Export symbols (for Windows DLL)
if(WIN32)
    target_compile_definitions(hlplayer_render PRIVATE
        HLPLAYER_RENDER_EXPORTS
    )
    add_custom_command(TARGET hlplayer_render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../../SDL2-2.30.8/x86_64-w64-mingw32/bin/SDL2.dll"
            $<TARGET_FILE_DIR:hlplayer_render>
    )
endif()

# Link D3D11/DXGI on Windows (used by D3D11RenderBridge)
if(WIN32)
    target_link_libraries(hlplayer_render PRIVATE d3d11 dxgi)
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS hlplayer_render
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer/render
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# install(EXPORT ...) - Exporting targets with FetchContent dependencies requires
# additional configuration. Disabled for scaffolding phase.
