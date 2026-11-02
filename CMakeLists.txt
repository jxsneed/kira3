cmake_minimum_required(VERSION 3.22)
project(KIRA VERSION 1.4.0)

# Runtime estatico: el plugin funciona sin instalar Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(KIRA
    COMPANY_NAME "KIRA Audio"
    BUNDLE_ID "audio.kira.vst"
    PLUGIN_MANUFACTURER_CODE KiRa
    PLUGIN_CODE KiR1
    FORMATS VST3 Standalone
    PRODUCT_NAME "KIRA"
    DESCRIPTION "Anime-inspired vocal pitch and energy effect"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(KIRA)
target_sources(KIRA PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Dsp.h
)
target_compile_features(KIRA PRIVATE cxx_std_17)
target_compile_definitions(KIRA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(KIRA PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
