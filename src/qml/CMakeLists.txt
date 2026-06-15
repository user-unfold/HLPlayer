# ============================================================================
# HLPlayer QML Integration
# ============================================================================
# This module provides QML plugin for Qt integration.
# It depends on Qt 6.7+ and provides QML components.
# ============================================================================

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick OpenGL)

# Collect source files
file(GLOB_RECURSE QML_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file(GLOB_RECURSE QML_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

# Add camera QML bridge if BUILD_QML and camera module exists
if(BUILD_QML AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../camera/src/QMLCameraRecorder.cpp")
    list(APPEND QML_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../camera/src/QMLCameraRecorder.cpp)
endif()

if(BUILD_QML AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../camera/include/hlplayer/QMLCameraRecorder.h")
    list(APPEND QML_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../camera/include/hlplayer/QMLCameraRecorder.h)
endif()

# QMLASRBridge needs to be compiled into hlplayer_qml (not hlplayer_asr)
# because qt6_add_qml_module(URI HLPlayer) only registers QML_ELEMENT types
# from its own target. hlplayer_asr has no QML module, so QMLASRBridge types
# would never be registered with the QML engine.
if(BUILD_ASR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../asr/src/QMLASRBridge.cpp")
    list(APPEND QML_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../asr/src/QMLASRBridge.cpp)
endif()
if(BUILD_ASR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../asr/include/hlplayer/QMLASRBridge.h")
    list(APPEND QML_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../asr/include/hlplayer/QMLASRBridge.h)
endif()

# Collect QML files (convert to relative paths for qt6_add_qml_module)
file(GLOB_RECURSE QML_FILES_ABSOLUTE
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qml
)

# Camera QML files are loaded at runtime via Qt.createComponent()
# from the filesystem — they do NOT need to be registered in the QML module.
# Including them here (with ../ paths) breaks qmlcachegen directory creation.
# if(BUILD_QML AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../camera/qml")
#     file(GLOB_RECURSE CAMERA_QML_FILES_ABSOLUTE
#         ${CMAKE_CURRENT_SOURCE_DIR}/../camera/qml/*.qml
#     )
#     list(APPEND QML_FILES_ABSOLUTE ${CAMERA_QML_FILES_ABSOLUTE})
# endif()

set(QML_FILES "")
foreach(_qml ${QML_FILES_ABSOLUTE})
    file(RELATIVE_PATH _qml_rel ${CMAKE_CURRENT_SOURCE_DIR} ${_qml})
    list(APPEND QML_FILES ${_qml_rel})
endforeach()

# Create the QML plugin library
add_library(hlplayer_qml SHARED
    ${QML_SOURCES}
    ${QML_HEADERS}
)

# Set library properties
set_target_properties(hlplayer_qml PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Qt automatic files (MOC, RCC, UIC)
set_target_properties(hlplayer_qml PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

# Include directories
target_include_directories(hlplayer_qml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/asr/include/hlplayer>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../core
        ${CMAKE_CURRENT_SOURCE_DIR}/../camera/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../camera/include/hlplayer
        ${CMAKE_SOURCE_DIR}/src/vsr/include
        ${CMAKE_SOURCE_DIR}/src/asr/include
        ${CMAKE_SOURCE_DIR}/src/crypto
)

# Optional FFmpeg headers
set(LOCAL_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1")
if(EXISTS "${LOCAL_FFMPEG_DIR}/include")
    target_include_directories(hlplayer_qml PRIVATE "${LOCAL_FFMPEG_DIR}/include")
endif()
if(EXISTS "${LOCAL_FFMPEG_DIR}/lib")
    target_link_directories(hlplayer_qml PRIVATE "${LOCAL_FFMPEG_DIR}/lib")
endif()

# Link Qt6 dependencies
target_link_libraries(hlplayer_qml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::OpenGL
        hlplayer_core
        hlplayer_render
        hlplayer_extractor
        hlplayer_crypto
    PRIVATE
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        avformat
        avcodec
        avutil
)

# Link camera module if BUILD_QML
if(BUILD_QML)
    target_link_libraries(hlplayer_qml PRIVATE hlplayer_camera)
    target_compile_definitions(hlplayer_qml PRIVATE HLPLAYER_CAMERA_QML_EXPORTS)
endif()

# Link ASR module if BUILD_ASR
if(BUILD_ASR)
    target_link_libraries(hlplayer_qml PRIVATE hlplayer_asr)
    target_compile_definitions(hlplayer_qml PRIVATE HLPLAYER_ASR_ENABLED)
endif()

if(BUILD_VSR)
    target_link_libraries(hlplayer_qml PRIVATE hlplayer_vsr)
    target_compile_definitions(hlplayer_qml PRIVATE HLPLAYER_VSR_ENABLED)
endif()

# Link crypto module for encryption export
target_link_libraries(hlplayer_qml PRIVATE hlplayer_crypto)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_qml PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_qml PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_qml PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_qml PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(hlplayer_qml PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
    # Workaround MinGW ld out-of-memory when linking large shared libraries
    target_link_options(hlplayer_qml PRIVATE "LINKER:--no-keep-memory")
endif()

# Export symbols (for Windows DLL)
if(WIN32)
    target_compile_definitions(hlplayer_qml PRIVATE
        HLPLAYER_QML_EXPORTS
        BUILD_QML
    )
else()
    target_compile_definitions(hlplayer_qml PRIVATE BUILD_QML)
endif()

# Link D3D11/DXGI on Windows (used by VideoOutputItem)
if(WIN32)
    target_link_libraries(hlplayer_qml PRIVATE d3d11 dxgi)
endif()

# QML module setup — QML_ELEMENT/QML_SINGLETON in C++ headers register types
# automatically via AUTOMOC. No explicit CMake-side type registration needed.
qt6_add_qml_module(hlplayer_qml
    URI HLPlayer
    VERSION ${PROJECT_VERSION}
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/HLPlayer
    NO_PLUGIN
    QML_FILES
        ${QML_FILES}
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS hlplayer_qml
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer/qml
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install QML module (if QML module was generated)
# install(FILES ${QT6_ADD_QML_MODULE_hlplayer_qml_QML_MODULE_OUTPUT}
#     DESTINATION ${CMAKE_INSTALL_DATADIR}/qt6/qml/HLPlayer
# )

# install(EXPORT ...) - Exporting targets with FetchContent dependencies requires
# additional configuration. Disabled for scaffolding phase.
