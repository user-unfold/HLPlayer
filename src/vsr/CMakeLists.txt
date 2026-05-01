# ============================================================================
# HLPlayer VSR Library (Video Super-Resolution)
# ============================================================================
# This module provides video super-resolution capabilities using NCNN.
# It requires Vulkan SDK to be installed for GPU acceleration.
# ============================================================================

# Collect source files
file(GLOB_RECURSE VSR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE VSR_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

# Create the VSR library as STATIC (per requirement)
add_library(hlplayer_vsr STATIC
    ${VSR_SOURCES}
    ${VSR_HEADERS}
)

# Set library properties
set_target_properties(hlplayer_vsr PROPERTIES
    VERSION ${PROJECT_VERSION}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Include directories
target_include_directories(hlplayer_vsr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        D:/ncnn/src
        ${CMAKE_BINARY_DIR}/ncnn/src
)

# Link dependencies
# ncnn linked PRIVATE to prevent its PUBLIC definitions (NOMINMAX, Threads)
# from propagating to hlplayer_qml and other consumers that don't use NCNN.
target_link_libraries(hlplayer_vsr
    PRIVATE
        ncnn
    PUBLIC
        hlplayer_core
)

# Ensure NCNN_STATIC_DEFINE is set when consuming ncnn headers
# (ncnn only sets this on its own target, not for consumers)
target_compile_definitions(hlplayer_vsr PRIVATE NCNN_STATIC_DEFINE)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_vsr PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_vsr PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_vsr PRIVATE
        _LINUX
    )
endif()

if(MSVC)
    target_compile_options(hlplayer_vsr PRIVATE
        /W4
    )
else()
    target_compile_options(hlplayer_vsr PRIVATE
        -Wall
        -Wextra
        -Wno-error
    )
endif()
