add_library(hlplayer_subtitle SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/SubtitleRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SubtitleRenderer.h
)

set_target_properties(hlplayer_subtitle PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(hlplayer_subtitle
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hlplayer_subtitle
    PUBLIC
        spdlog::spdlog
)

target_compile_definitions(hlplayer_subtitle PRIVATE
    HLPLAYER_SUBTITLE_EXPORTS
)

if(MSVC)
    target_compile_options(hlplayer_subtitle PRIVATE /W4 /WX)
    target_compile_definitions(hlplayer_subtitle PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        HLPLAYER_SUBTITLE_EXPORTS
    )
else()
    target_compile_options(hlplayer_subtitle PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-attributes
    )
endif()

install(TARGETS hlplayer_subtitle
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
