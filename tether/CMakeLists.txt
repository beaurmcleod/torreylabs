cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(Tether VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Static MSVC runtime: the plugin loads without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TETHER_BUILD_TESTS "Build the unit test runner" ON)
option(TETHER_BUILD_TOOLS "Build the offline render tool" ON)
option(TETHER_COPY_AFTER_BUILD "Install the plugin into the system plugin folders after building" OFF)
set(TETHER_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading one")

#-------------------------------------------------------------------------------
# JUCE
#-------------------------------------------------------------------------------
if(TETHER_JUCE_DIR)
    add_subdirectory("${TETHER_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#-------------------------------------------------------------------------------
# DSP engine: plain C++ on top of juce_dsp, compiled into every target that
# links it (JUCE modules are interface libraries, so this avoids ODR clashes).
#-------------------------------------------------------------------------------
add_library(tether_dsp INTERFACE)
target_sources(tether_dsp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/PitchTracker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/SpectralLayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/TetherEngine.cpp")
target_include_directories(tether_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

set(TETHER_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Inter (SIL Open Font License), embedded so the UI looks the same everywhere.
juce_add_binary_data(TetherBinaryData SOURCES
    Resources/fonts/Inter-Medium.ttf
    Resources/fonts/Inter-ExtraBold.ttf)

set(TETHER_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/ui/TetherLookAndFeel.cpp
    Source/ui/Visualizer.cpp)

#-------------------------------------------------------------------------------
# Plugin
#-------------------------------------------------------------------------------
juce_add_plugin(Tether
    PRODUCT_NAME "Tether"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Low End Candy"
    COMPANY_WEBSITE "https://lowendcandy.com"
    BUNDLE_ID com.lowendcandy.tether
    DESCRIPTION "Locks a layer to a guide sound: pitch, level and motion follow the sidechain"
    PLUGIN_MANUFACTURER_CODE Lecd
    PLUGIN_CODE Tthr
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${TETHER_COPY_AFTER_BUILD})

target_sources(Tether PRIVATE ${TETHER_PLUGIN_SOURCES})
target_compile_definitions(Tether PUBLIC ${TETHER_COMMON_DEFINITIONS})
target_link_libraries(Tether
    PRIVATE
        tether_dsp
        TetherBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#-------------------------------------------------------------------------------
# Unit tests
#-------------------------------------------------------------------------------
if(TETHER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(TetherTests PRODUCT_NAME "TetherTests")
    target_sources(TetherTests PRIVATE
        tests/TestMain.cpp
        tests/DspTests.cpp
        tests/PluginTests.cpp
        ${TETHER_PLUGIN_SOURCES})
    target_compile_definitions(TetherTests PRIVATE
        ${TETHER_COMMON_DEFINITIONS}
        TETHER_TESTS=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TetherTests
        PRIVATE
            tether_dsp
            TetherBinaryData
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME TetherTests COMMAND TetherTests)
endif()

#-------------------------------------------------------------------------------
# Offline renderer: tether_render layer.wav guide.wav out.wav [--param value]
#-------------------------------------------------------------------------------
if(TETHER_BUILD_TOOLS)
    juce_add_console_app(TetherRender PRODUCT_NAME "tether_render")
    set_target_properties(TetherRender PROPERTIES OUTPUT_NAME "tether_render")
    target_sources(TetherRender PRIVATE tools/TetherRender.cpp)
    target_compile_definitions(TetherRender PRIVATE ${TETHER_COMMON_DEFINITIONS})
    target_link_libraries(TetherRender
        PRIVATE
            tether_dsp
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
