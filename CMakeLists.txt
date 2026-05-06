cmake_minimum_required(VERSION 3.22)
project(HLPlayer VERSION 1.1.1 LANGUAGES CXX)

# ============================================================================
# Project Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
enable_testing()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# FetchContent Dependencies
# ============================================================================
include(FetchContent)

# spdlog - Logging library (v1.17.0)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# nlohmann/json - JSON library (v3.11.3)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# concurrentqueue - Lock-free queue (v1.0.3)
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(concurrentqueue)

# Catch2 - Testing framework (v3.5.4)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

# Google Benchmark - Benchmarking library (v1.8.3)
# Using older stable version to avoid compilation issues
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable GTEST in benchmark")
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing")
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(benchmark)

# ============================================================================
# Compile-time Options
# ============================================================================
# Compile-time Options
option(TELEMETRY_ENABLED "Enable OpenTelemetry telemetry" OFF)
option(TRACE_ENABLED "Enable trace-level logging" OFF)

# ============================================================================
# Camera Recording + Streaming Module
# ============================================================================
option(BUILD_CAMERA "Build Camera Recording + Streaming module" ON)

# ============================================================================
# NCNN (Video Super-Resolution dependency)
# ============================================================================
option(BUILD_VSR "Build VSR (Video Super-Resolution) module" OFF)

# ============================================================================
# Subdirectories (core modules first, NCNN last to minimize pollution)
# ============================================================================
add_subdirectory(src/core)
add_subdirectory(src/extractor)
add_subdirectory(src/render)

# Camera Recording + Streaming module
if(BUILD_CAMERA)
    add_subdirectory(src/camera)
endif()

option(BUILD_QML "Build Qt6/QML integration module" ON)
if(BUILD_QML)
    add_subdirectory(src/qml)
    add_subdirectory(src/app)
endif()

# ============================================================================
# NCNN integration — placed AFTER core modules to isolate CMake state
# NCNN's CMakeLists.txt modifies global policies (CMP0091) and variables
# that can affect downstream targets. Processing it last limits the damage.
# ============================================================================
if(BUILD_VSR)
    # Save critical CMake state before NCNN pollutes it
    set(_SAVE_CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    set(_SAVE_CMAKE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    set(_SAVE_CMAKE_CXX_STANDARD_REQUIRED "${CMAKE_CXX_STANDARD_REQUIRED}")

    # Pre-set NCNN options as CACHE variables before add_subdirectory
    set(NCNN_VULKAN ON CACHE BOOL "Enable Vulkan GPU acceleration" FORCE)
    set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(NCNN_DISABLE_RTTI OFF CACHE BOOL "" FORCE)
    set(NCNN_DISABLE_EXCEPTION OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_WITH_STATIC_CRT OFF CACHE BOOL "" FORCE)

    # Use local NCNN source (D:/ncnn) instead of FetchContent
    add_subdirectory(D:/ncnn ${CMAKE_BINARY_DIR}/ncnn EXCLUDE_FROM_ALL)

    # Restore critical CMake state that NCNN may have corrupted
    set(CMAKE_BUILD_TYPE "${_SAVE_CMAKE_BUILD_TYPE}" CACHE STRING "Build type" FORCE)
    set(CMAKE_CXX_STANDARD "${_SAVE_CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD_REQUIRED "${_SAVE_CMAKE_CXX_STANDARD_REQUIRED}")

    add_subdirectory(src/vsr)
endif()

add_subdirectory(tests)

# ============================================================================
# Configuration Summary
# ============================================================================
message(STATUS "=========================================")
message(STATUS "HLPlayer Configuration Summary")
message(STATUS "=========================================")
message(STATUS "Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "=========================================")
