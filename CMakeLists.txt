cmake_minimum_required(VERSION 3.15)

project(Gridiron
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal by default so one bundle loads in both the Apple Silicon and the
# Intel build of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64 for a
# faster dev build.
#
# This must sit immediately after project() and before any target exists. CMake
# latches the architecture list when the first target is created; setting it
# later is silently ignored and you get an arm64-only binary that the build log
# calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and FFGL's plugin interface is
# still OpenGL, so every SDK call warns. There is no alternative API to move to
# here, so silencing it is not ignoring a problem.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# windows.h defines min and max as macros, which turns every std::min into
# `std::(...)` and fails with an error naming neither min nor windows.h. Set
# globally rather than per-file: the next file to include windows.h hits it too,
# and the error gives no hint what happened.
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(GRIDIRON_BUILD_TOOLS "Build the offline render and measurement harness" ON)


# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule pinned to b1afaf9 -- the rev the
# rest of the fleet pins.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything the plugin shares, as an OBJECT library.
#
# OBJECT and not STATIC on purpose. `CFFGLPluginInfo` registers itself from a
# file-scope constructor and nothing ever references that object by name, so in
# an archive the linker is entitled to drop the whole translation unit -- and
# the result is a bundle that loads, exports plugMain, and reports that it
# contains no plugins.
#
# Note what is NOT in here: SourcePlugin.cpp. It carries the CFFGLPluginInfo and
# is listed directly in its own MODULE target below.
# ---------------------------------------------------------------------------
add_library(gridiron_core OBJECT
    source/Hash.h
    source/Mat4.h
    source/Controls.h
    source/Clock.h
    source/Clock.cpp
    source/SvgLib.h
    source/SvgLib.cpp
    source/Library.h
    source/Library.cpp
    source/Fill.h
    source/Fill.cpp
    source/Layout.h
    source/Layout.cpp
    source/Atlas.h
    source/Atlas.cpp
    source/Shaders.h
    source/Shaders.cpp
    source/Render.h
    source/Render.cpp
    source/Gridiron.h
    source/Gridiron.cpp
)

target_include_directories(gridiron_core PUBLIC source external/stb external/nanosvg)
target_link_libraries(gridiron_core PUBLIC ffgl::sdk)
target_compile_definitions(gridiron_core PUBLIC GRIDIRON_VERSION="${PROJECT_VERSION}")
set_target_properties(gridiron_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# std::filesystem needs an explicit link on older GCC. Harmless where it does not.
if(NOT APPLE AND NOT MSVC)
    target_link_libraries(gridiron_core PUBLIC stdc++fs)
endif()
find_package(Threads REQUIRED)
target_link_libraries(gridiron_core PUBLIC Threads::Threads)

# On Windows/Linux the SDK headers pull in <GL/glew.h>. The SDK links GLEW
# privately, so consumers need it too -- for the include path and for the
# GLEW_STATIC define the static vcpkg triplet propagates. macOS uses the system
# OpenGL framework and needs none of this. GLEW arrives through vcpkg.json,
# which nothing in this file points at: lose it and only Windows breaks, and
# only at configure time.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(gridiron_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The plugin. FFGL resolves one plugMain per binary.
# ---------------------------------------------------------------------------
function(gridiron_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE gridiron_core)

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            OUTPUT_NAME "${bundle_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
        # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
        target_link_libraries(${target} PRIVATE
            "-framework OpenGL"
            "-framework Carbon"
            "-framework AppKit"
        )
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
    endif()
endfunction()

gridiron_add_plugin(GridironSource source/SourcePlugin.cpp
    "Gridiron" "com.stoatworks.ffgl.gridiron")

# ---------------------------------------------------------------------------
# Offline harness. Fill, Library and Layout are pure arithmetic and are checked
# here without a GL context or a host -- which is where this plugin is actually
# verified, as against merely built.
# ---------------------------------------------------------------------------
if(GRIDIRON_BUILD_TOOLS)
    add_executable(gdtest_fill    tools/gdtest/Fill_test.cpp    source/Fill.cpp)
    add_executable(gdtest_layout  tools/gdtest/Layout_test.cpp  source/Layout.cpp source/Fill.cpp)
    add_executable(gdtest_library tools/gdtest/Library_test.cpp source/Library.cpp source/SvgLib.cpp)

    foreach(t gdtest_fill gdtest_layout gdtest_library)
        target_include_directories(${t} PRIVATE source external/stb external/nanosvg)
        target_compile_features(${t} PRIVATE cxx_std_17)
        if(NOT APPLE AND NOT MSVC)
            target_link_libraries(${t} PRIVATE stdc++fs)
        endif()
    endforeach()
    target_link_libraries(gdtest_library PRIVATE Threads::Threads)

    # The pixel harness is macOS-only: it stands up a CGL core context directly.
    # oxbow covers registration and instantiation on every platform; this covers
    # the one thing oxbow cannot reach, which is a loaded folder.
    if(APPLE)
        # The project video's footage: the real plugin class driven by a cue
        # sheet, in a headless context. macOS-only for the same reason as the
        # pixel harness -- it stands up CGL directly.
        add_executable(gdseq tools/gdseq/main.mm)
        target_link_libraries(gdseq PRIVATE gridiron_core "-framework OpenGL")
        target_compile_options(gdseq PRIVATE -Wno-deprecated-declarations)

        add_executable(gdtest_render tools/gdtest/Render_test.mm)
        target_link_libraries(gdtest_render PRIVATE gridiron_core "-framework OpenGL")
        target_compile_options(gdtest_render PRIVATE -Wno-deprecated-declarations)
    endif()

    enable_testing()
    add_test(NAME fill   COMMAND gdtest_fill)
    add_test(NAME layout COMMAND gdtest_layout)
    add_test(NAME library COMMAND gdtest_library "${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures")
    if(APPLE)
        add_test(NAME render COMMAND gdtest_render "${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures")
    endif()
endif()
