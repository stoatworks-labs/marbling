cmake_minimum_required(VERSION 3.15)

project(Marbling
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal build by default so the plugin loads in both native Apple Silicon
# and Intel builds of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64
# for a faster single-arch dev build.
#
# This must sit immediately after project() and before any target is defined.
# CMake latches the architecture list when the first target is created, and
# setting it later is silently ignored -- you get an arm64-only binary that the
# build log calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and Resolume's plugin interface
# is still OpenGL, so every SDK call warns. Silencing it is not ignoring a
# problem -- there is no alternative API to move to here.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
endif()

option(MARBLING_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule under external/.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# earshot: the fleet's live-audio front end, a snapshot (not a submodule) in
# external/earshot. external/earshot/SNAPSHOT says which commit, and that it
# is unpatched. It builds as a static library inside this project; its own
# tools are only built when it is the top-level project.
# ---------------------------------------------------------------------------
add_subdirectory(external/earshot EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything the plugin is, as an OBJECT library so the bundle and the
# offline harness are built from exactly the same code.
#
# OBJECT and not STATIC on purpose. CFFGLPluginInfo registers itself from a
# file-scope constructor and nothing ever references that object by name, so
# in an archive the linker is entitled to drop the whole translation unit --
# and the result is a bundle that loads, exports plugMain, and reports that it
# contains no plugins.
# ---------------------------------------------------------------------------
add_library(marbling_core OBJECT
    source/Diag.h
    source/Diag.cpp
    source/Controls.h
    source/Controls.cpp
    source/Bath.h
    source/Bath.cpp
    source/Audio.h
    source/Audio.cpp
    source/PassBuffer.h
    source/PassBuffer.cpp
    source/GLState.h
    source/Shaders.h
    source/Shaders.cpp
    source/Marbling.h
    source/Marbling.cpp
)

target_include_directories(marbling_core PUBLIC source)
target_link_libraries(marbling_core PUBLIC ffgl::sdk earshot::earshot)
target_compile_definitions(marbling_core PUBLIC MARBLING_VERSION="${PROJECT_VERSION}")
set_target_properties(marbling_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# On Windows/Linux the SDK headers we include pull in <GL/glew.h>. The SDK links
# GLEW privately, so consumers need it too for the include path (and for the
# GLEW_STATIC define the static vcpkg triplet propagates). macOS uses the system
# OpenGL framework and needs none of this.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(marbling_core PUBLIC GLEW::GLEW)
endif()

# The plugin. EffectPlugin.cpp carries the CFFGLPluginInfo and is listed in
# the MODULE target alone: in the OBJECT library the harness would register
# it too.
# ---------------------------------------------------------------------------
function(marbling_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE marbling_core)

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

marbling_add_plugin(MarblingEffect source/EffectPlugin.cpp
    "Marbling" "com.stoatworks.ffgl.marbling")

# ---------------------------------------------------------------------------
# Offline harness. Whether every tool preserves area, whether a drop pushes
# older ink to sqrt(rho^2 + r^2), whether a reverse stroke undoes its stroke,
# what one bake's resampling costs, whether the triggers land where they
# should at any frame rate: measurements read from the rendered picture, so
# this is where the plugin is verified rather than merely previewed. No check
# opens an audio device: earshot's feed() and a scripted backend.
# ---------------------------------------------------------------------------
if(MARBLING_BUILD_TOOLS AND APPLE)
    add_executable(matest tools/matest/main.cpp)
    # zlib ships with the OS, which is why the PNG writer is fifty lines here
    # rather than a vendored dependency.
    target_link_libraries(matest PRIVATE marbling_core z
        "-framework OpenGL"
        "-framework AppKit"
    )
    # The interposer --coreaudio loads into a child matest: it counts (and
    # can refuse) every dlopen of an audio framework and every CoreAudio
    # call, so "nothing touched CoreAudio" is measured, not assumed.
    add_library(audiospy SHARED tools/matest/audiospy.c)
    set_target_properties(audiospy PROPERTIES PREFIX "lib" OUTPUT_NAME "audiospy")
    target_link_libraries(audiospy PRIVATE "-framework CoreAudio" "-framework AudioToolbox")
    add_dependencies(matest audiospy)
endif()

# ---------------------------------------------------------------------------
# `cmake --install` drops the bundle straight into the user's Resolume plugin
# folder. Override with -DCMAKE_INSTALL_PREFIX=...
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # honour an explicit prefix
elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS MarblingEffect
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)
