# ============================================================================
# HLPlayer Camera Module Tests
# ============================================================================

# Test executable
add_executable(camera_test
    camera_test.cpp
)

# Include directories for dependencies
target_include_directories(camera_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/extractor
)

# Link against the camera library and required dependencies
target_link_libraries(camera_test
    PRIVATE
        hlplayer_camera
        Catch2::Catch2WithMain
)

# Enable auto-registration of tests
target_compile_features(camera_test PRIVATE cxx_std_20)

# Get MinGW bin directory from CXX compiler path
get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

# Windows: copy DLLs next to test executable so catch_discover_tests can find them
if(WIN32)
    add_custom_command(TARGET camera_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:hlplayer_core>
            $<TARGET_FILE:hlplayer_camera>
            $<TARGET_FILE_DIR:camera_test>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MINGW_BIN_DIR}/libstdc++-6.dll
            ${MINGW_BIN_DIR}/libgcc_s_seh-1.dll
            ${MINGW_BIN_DIR}/libwinpthread-1.dll
            $<TARGET_FILE_DIR:camera_test>
    )
endif()

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(camera_test)
