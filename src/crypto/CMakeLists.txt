# ============================================================================
# HLPlayer Crypto Module
# ============================================================================
# Provides AES-256-CTR, HMAC-SHA256, PBKDF2, constant-time comparison,
# and secure memory allocation for encrypted video export (.hlv format).
# ============================================================================

# Find Qt6 Core (required for QObject, Q_OBJECT, signals/slots)
find_package(Qt6 6.5 COMPONENTS Core REQUIRED)

# Find FFmpeg package
set(LOCAL_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-7.1.1")

if(EXISTS "${LOCAL_FFMPEG_DIR}")
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIRS "${LOCAL_FFMPEG_DIR}/include")
    set(FFMPEG_LIBRARY_DIRS "${LOCAL_FFMPEG_DIR}/lib")
    set(FFMPEG_LIBRARIES avutil avformat)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET libavutil)
    endif()
endif()

set(CRYPTO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/AesCtr256.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HmacSha256.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pbkdf2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HlvHeader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DecryptingAVIOContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileEncryptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KeyManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EncryptionExporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionKeyCache.cpp
)

set(CRYPTO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/CryptoExport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AesCtr256.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HmacSha256.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pbkdf2.h
    ${CMAKE_CURRENT_SOURCE_DIR}/constant_time.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SecureAllocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HlvHeader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DecryptingAVIOContext.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FileEncryptor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/KeyManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/EncryptionExporter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionKeyCache.h
)

# Create the crypto library
add_library(hlplayer_crypto SHARED
    ${CRYPTO_SOURCES}
    ${CRYPTO_HEADERS}
)

# Set library properties
set_target_properties(hlplayer_crypto PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

# Include directories
target_include_directories(hlplayer_crypto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${FFMPEG_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src/core/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(hlplayer_crypto
    PUBLIC
        hlplayer_core
    PRIVATE
        ${FFMPEG_LIBRARIES}
        Qt6::Core
)

if(FFMPEG_FOUND)
    target_link_directories(hlplayer_crypto PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(hlplayer_crypto PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_definitions(hlplayer_crypto PRIVATE
        __APPLE__
    )
elseif(UNIX)
    target_compile_definitions(hlplayer_crypto PRIVATE
        _LINUX
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hlplayer_crypto PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(hlplayer_crypto PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Export symbols (for Windows DLL)
if(WIN32)
    target_compile_definitions(hlplayer_crypto PRIVATE
        HLPLAYER_CRYPTO_EXPORTS
    )
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS hlplayer_crypto
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hlplayer/crypto
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
