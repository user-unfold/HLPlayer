# ============================================================================
# HLPlayer End-to-End Tests
# ============================================================================
# Integration tests for VSR pipeline components and workflows
# ============================================================================

# Collect test sources
file(GLOB E2E_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

# Create test executable
add_executable(hlplayer_e2e_tests
    ${E2E_TEST_SOURCES}
)

# Set test properties
set_target_properties(hlplayer_e2e_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(hlplayer_e2e_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/render
)

# Link dependencies
target_link_libraries(hlplayer_e2e_tests
    PRIVATE
        Catch2::Catch2
        Catch2::Catch2WithMain
        hlplayer_core
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

# Compile-time options
if(TELEMETRY_ENABLED)
    target_compile_definitions(hlplayer_e2e_tests PRIVATE TELEMETRY_ENABLED)
endif()

if(TRACE_ENABLED)
    target_compile_definitions(hlplayer_e2e_tests PRIVATE TRACE_ENABLED)
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_e2e_tests PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_e2e_tests PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_e2e_tests PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_e2e_tests PRIVATE
        /W4
    )
else()
    target_compile_options(hlplayer_e2e_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Register tests with CTest
include(CTest)
include(Catch)

set_target_properties(hlplayer_e2e_tests PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/core;${CMAKE_BINARY_DIR}/src/extractor;${CMAKE_BINARY_DIR}/src/render"
    BUILD_WITH_INSTALL_RPATH FALSE
)

# Get MinGW bin directory from CXX compiler path
get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

# Windows: copy DLLs next to test executable so catch_discover_tests can find them
if(WIN32)
    add_custom_command(TARGET hlplayer_e2e_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:hlplayer_core>
            $<TARGET_FILE_DIR:hlplayer_e2e_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MINGW_BIN_DIR}/libstdc++-6.dll
            ${MINGW_BIN_DIR}/libgcc_s_seh-1.dll
            ${MINGW_BIN_DIR}/libwinpthread-1.dll
            $<TARGET_FILE_DIR:hlplayer_e2e_tests>
    )
endif()

catch_discover_tests(hlplayer_e2e_tests)

# Install test executable
install(TARGETS hlplayer_e2e_tests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
