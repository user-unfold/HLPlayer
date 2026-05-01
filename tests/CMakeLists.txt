# ============================================================================
# HLPlayer Tests and Benchmarks
# ============================================================================
# This directory contains unit tests and benchmarks for the HLPlayer SDK.
# Uses Catch2 for testing and Google Benchmark for performance measurement.
# ============================================================================

# Add end-to-end tests subdirectory
add_subdirectory(e2e)

# Collect test sources (non-recursive to exclude benchmarks directory)
file(GLOB TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

# Create test executable
add_executable(hlplayer_tests
    ${TEST_SOURCES}
)

# Set test properties
set_target_properties(hlplayer_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(hlplayer_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/render
)

# Link dependencies
target_link_libraries(hlplayer_tests
    PRIVATE
        Catch2::Catch2
        Catch2::Catch2WithMain
        hlplayer_core
        hlplayer_extractor
        hlplayer_render
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

# Compile-time options
if(TELEMETRY_ENABLED)
    target_compile_definitions(hlplayer_tests PRIVATE TELEMETRY_ENABLED)
endif()

if(TRACE_ENABLED)
    target_compile_definitions(hlplayer_tests PRIVATE TRACE_ENABLED)
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_tests PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_tests PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_tests PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_tests PRIVATE
        /W4
    )
else()
    target_compile_options(hlplayer_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Register tests with CTest
include(CTest)
include(Catch)

set_target_properties(hlplayer_tests PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/core;${CMAKE_BINARY_DIR}/src/extractor;${CMAKE_BINARY_DIR}/src/render"
    BUILD_WITH_INSTALL_RPATH FALSE
)

# Get MinGW bin directory from CXX compiler path
# CMAKE_CXX_COMPILER will be like D:/msys64/ucrt64/bin/g++.exe
get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

# Windows: copy DLLs next to test executable so catch_discover_tests can find them
if(WIN32)
    add_custom_command(TARGET hlplayer_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:hlplayer_core>
            $<TARGET_FILE:hlplayer_extractor>
            $<TARGET_FILE:hlplayer_render>
            $<TARGET_FILE_DIR:hlplayer_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MINGW_BIN_DIR}/libstdc++-6.dll
            ${MINGW_BIN_DIR}/libgcc_s_seh-1.dll
            ${MINGW_BIN_DIR}/libwinpthread-1.dll
            $<TARGET_FILE_DIR:hlplayer_tests>
    )
endif()

catch_discover_tests(hlplayer_tests)

# ============================================================================
# Benchmark executable
# ============================================================================
# Collect benchmark sources
file(GLOB_RECURSE BENCHMARK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.c
)

# Create benchmark executable if benchmark sources exist
if(BENCHMARK_SOURCES)
    add_executable(hlplayer_benchmarks
        ${BENCHMARK_SOURCES}
    )

    set_target_properties(hlplayer_benchmarks PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    # Include directories
    target_include_directories(hlplayer_benchmarks
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src/core
            ${CMAKE_SOURCE_DIR}/src/extractor
            ${CMAKE_SOURCE_DIR}/src/render
    )

    # Link dependencies
    target_link_libraries(hlplayer_benchmarks
        PRIVATE
            benchmark::benchmark
            benchmark::benchmark_main
            hlplayer_core
            hlplayer_extractor
            hlplayer_render
            spdlog::spdlog
            nlohmann_json::nlohmann_json
    )

    # Platform-specific settings
    if(WIN32)
        target_compile_definitions(hlplayer_benchmarks PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    elseif(APPLE)
        target_compile_definitions(hlplayer_benchmarks PRIVATE
            __APPLE__
        )
    elseif(UNIX)
        target_compile_definitions(hlplayer_benchmarks PRIVATE
            _LINUX
        )
    endif()

    # Compiler warnings
    if(MSVC)
        target_compile_options(hlplayer_benchmarks PRIVATE
            /W4
        )
    else()
        target_compile_options(hlplayer_benchmarks PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    # Windows: copy DLLs next to benchmark executable
    if(WIN32)
        add_custom_command(TARGET hlplayer_benchmarks POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:hlplayer_core>
                $<TARGET_FILE:hlplayer_extractor>
                $<TARGET_FILE:hlplayer_render>
                $<TARGET_FILE_DIR:hlplayer_benchmarks>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MINGW_BIN_DIR}/libstdc++-6.dll
                ${MINGW_BIN_DIR}/libgcc_s_seh-1.dll
                ${MINGW_BIN_DIR}/libwinpthread-1.dll
                $<TARGET_FILE_DIR:hlplayer_benchmarks>
        )
    endif()
endif()

# Install test and benchmark executables
install(TARGETS hlplayer_tests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BENCHMARK_SOURCES)
    install(TARGETS hlplayer_benchmarks
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
