cmake_minimum_required(VERSION 3.22)
project(Roomprint VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ROOMPRINT_DSP_ONLY "Only build the stand-alone DSP test suite (no JUCE download)" OFF)

# --- DSP test suite (no JUCE needed) -------------------------------------------------
enable_testing()
add_executable(roomprint_dsp_tests Tests/test_engine.cpp)
target_include_directories(roomprint_dsp_tests PRIVATE Source/dsp)
if(NOT MSVC)
    target_compile_options(roomprint_dsp_tests PRIVATE -O2)
endif()
add_test(NAME roomprint_dsp COMMAND roomprint_dsp_tests)

if(ROOMPRINT_DSP_ONLY)
    return()
endif()

# --- JUCE plugin -----------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Roomprint
    COMPANY_NAME                Roomprint
    PLUGIN_MANUFACTURER_CODE    Rmpr
    PLUGIN_CODE                 Rp01
    PRODUCT_NAME                "Roomprint"
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Fx Reverb
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)   # set TRUE to install into the system VST3 folder after each build

target_sources(Roomprint PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/dsp/Common.h
    Source/dsp/Oversampler.h
    Source/dsp/Saturator.h
    Source/dsp/SourceStage.h
    Source/dsp/Room.h
    Source/dsp/Engine.h)

target_include_directories(Roomprint PRIVATE Source)

target_compile_definitions(Roomprint PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Roomprint
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
