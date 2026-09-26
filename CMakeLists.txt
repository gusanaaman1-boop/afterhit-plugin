cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    # Without this the binary's minimum macOS is the build machine's SDK and the
    # plug-in silently refuses to load on any older Mac.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(AfterHit VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 9 - the shared ~/JUCE checkout every plug-in in this workspace uses; CI
# checks it out into ./JUCE instead.
if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/JUCE/CMakeLists.txt")
    set(AH_JUCE_DIR "$ENV{HOME}/JUCE")
elseif(DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/JUCE/CMakeLists.txt")
    set(AH_JUCE_DIR "$ENV{USERPROFILE}/JUCE")
else()
    set(AH_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
add_subdirectory(${AH_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# --- build identity ------------------------------------------------------------
set(AH_VERSION "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE AH_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT AH_GIT_DESCRIBE)
    set(AH_GIT_DESCRIBE "no-git")
endif()
string(TIMESTAMP AH_BUILD_DATE "%Y-%m-%d" UTC)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/AfterHitVersion.h.in"
               "${CMAKE_BINARY_DIR}/generated/AfterHitVersion.h" @ONLY)
message(STATUS "AFTERHIT ${AH_VERSION} (${AH_GIT_DESCRIBE}, ${AH_BUILD_DATE})")

if(APPLE)
    set(AH_FORMATS VST3 AU Standalone)
else()
    set(AH_FORMATS VST3 Standalone)
endif()

option(AH_COPY_AFTER_BUILD "Copy built plug-ins into the user plug-in folders" ON)
option(AH_WERROR "Treat warnings in AFTERHIT's own sources as errors" OFF)
option(AH_SANITIZERS "Add AfterHitTestsSan / AfterHitHostTestsSan targets built with ASan/UBSan" OFF)

if(MSVC)
    # /utf-8: without it MSVC reads the source in the machine's ANSI codepage
    # and the first non-ASCII character in a comment is a Windows-only error.
    set(AH_WARNING_FLAGS /W4 /permissive- /utf-8)
    if(AH_WERROR)
        list(APPEND AH_WARNING_FLAGS /WX)
    endif()
else()
    set(AH_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wunused
        -Woverloaded-virtual -Wnon-virtual-dtor -Wimplicit-fallthrough -Wfloat-conversion)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND AH_WARNING_FLAGS -Wshorten-64-to-32)
    endif()
    if(AH_WERROR)
        list(APPEND AH_WARNING_FLAGS -Werror)
    endif()
endif()

set(AH_DSP_SOURCES
    Source/Dsp/AfterHitEngine.cpp
    Source/Dsp/Reverb.cpp)

set(AH_PLUGIN_SOURCES
    ${AH_DSP_SOURCES}
    Source/Core/Parameters.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Ui/TimingStrip.cpp
    Source/Ui/Theme.cpp)

set(AH_OWN_SOURCES ${AH_PLUGIN_SOURCES}
    Source/Tools/test_dsp.cpp Source/Tools/test_host.cpp
    Source/Tools/screenshot_tool.cpp Source/Tools/render_tool.cpp)
set_source_files_properties(${AH_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "${AH_WARNING_FLAGS}")

set(AH_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# The bundle icon is rendered BY the plug-in's own code (AfterHitShot --icon)
# and committed; on a fresh tree it may not exist yet.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png")
    set(AH_ICON_ARG ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png")
endif()

# --- the plug-in ---------------------------------------------------------------
# PLUGIN_CODE and BUNDLE_ID are frozen from 1.0: a host identifies an installed
# plug-in (and its automation) by them.
juce_add_plugin(AfterHit
    COMPANY_NAME "Naaman"
    BUNDLE_ID "com.naaman.afterhit"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${AH_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Naam
    PLUGIN_CODE Afht
    FORMATS ${AH_FORMATS}
    VST3_CATEGORIES "Fx" "Reverb" "Dynamics"
    AU_MAIN_TYPE kAudioUnitType_Effect
    VERSION "${AH_VERSION}"
    ${AH_ICON_ARG}
    PRODUCT_NAME "AFTERHIT")

target_sources(AfterHit PRIVATE ${AH_PLUGIN_SOURCES})
target_include_directories(AfterHit PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(AfterHit PUBLIC ${AH_COMMON_DEFS})
target_link_libraries(AfterHit
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# --- DSP measurement suite -----------------------------------------------------
#     build/AfterHitTests_artefacts/<config>/AfterHitTests
juce_add_console_app(AfterHitTests PRODUCT_NAME "AfterHitTests")
juce_generate_juce_header(AfterHitTests)
target_sources(AfterHitTests PRIVATE Source/Tools/test_dsp.cpp ${AH_DSP_SOURCES})
target_include_directories(AfterHitTests PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(AfterHitTests PUBLIC ${AH_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(AfterHitTests
    PRIVATE juce::juce_audio_basics juce::juce_core
    PUBLIC  juce::juce_recommended_config_flags)

# --- host-contract suite -------------------------------------------------------
#     build/AfterHitHostTests_artefacts/<config>/AfterHitHostTests
# Drives the whole AudioProcessor the way a host does, editor included.
juce_add_console_app(AfterHitHostTests PRODUCT_NAME "AfterHitHostTests")
juce_generate_juce_header(AfterHitHostTests)
target_sources(AfterHitHostTests PRIVATE Source/Tools/test_host.cpp ${AH_PLUGIN_SOURCES})
target_include_directories(AfterHitHostTests PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(AfterHitHostTests PUBLIC ${AH_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(AfterHitHostTests
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags)

# --- deterministic UI renderer -------------------------------------------------
#     build/AfterHitShot_artefacts/<config>/AfterHitShot ui-shots
juce_add_console_app(AfterHitShot PRODUCT_NAME "AfterHitShot")
juce_generate_juce_header(AfterHitShot)
target_sources(AfterHitShot PRIVATE Source/Tools/screenshot_tool.cpp ${AH_PLUGIN_SOURCES})
target_include_directories(AfterHitShot PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(AfterHitShot PUBLIC ${AH_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(AfterHitShot
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags)

# --- offline renderer for listening checks --------------------------------------
#     build/AfterHitRender_artefacts/<config>/AfterHitRender in.wav out.wav [preset] [id=value ...]
juce_add_console_app(AfterHitRender PRODUCT_NAME "AfterHitRender")
juce_generate_juce_header(AfterHitRender)
target_sources(AfterHitRender PRIVATE Source/Tools/render_tool.cpp ${AH_PLUGIN_SOURCES})
target_include_directories(AfterHitRender PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(AfterHitRender PUBLIC ${AH_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(AfterHitRender
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags)

# --- sanitiser builds of both suites (opt-in) ----------------------------------
#     cmake -B build-san -DAH_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
if(AH_SANITIZERS)
    juce_add_console_app(AfterHitTestsSan PRODUCT_NAME "AfterHitTestsSan")
    juce_generate_juce_header(AfterHitTestsSan)
    target_sources(AfterHitTestsSan PRIVATE Source/Tools/test_dsp.cpp ${AH_DSP_SOURCES})
    target_include_directories(AfterHitTestsSan PUBLIC "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(AfterHitTestsSan PUBLIC ${AH_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(AfterHitTestsSan PRIVATE juce::juce_audio_basics juce::juce_core PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(AfterHitHostTestsSan PRODUCT_NAME "AfterHitHostTestsSan")
    juce_generate_juce_header(AfterHitHostTestsSan)
    target_sources(AfterHitHostTestsSan PRIVATE Source/Tools/test_host.cpp ${AH_PLUGIN_SOURCES})
    target_include_directories(AfterHitHostTestsSan PUBLIC "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(AfterHitHostTestsSan PUBLIC ${AH_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AfterHitHostTestsSan PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra PUBLIC juce::juce_recommended_config_flags)

    foreach(t AfterHitTestsSan AfterHitHostTestsSan)
        if(MSVC)
            target_compile_options(${t} PRIVATE /fsanitize=address)
        else()
            target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
            target_link_options(${t} PRIVATE -fsanitize=address,undefined)
        endif()
    endforeach()
endif()
