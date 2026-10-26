cmake_minimum_required(VERSION 3.22)
project(NSAChords VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(NSAChords
  COMPANY_NAME "Night Session Audio"
  PLUGIN_MANUFACTURER_CODE Nsau
  PLUGIN_CODE Nsch
  FORMATS VST3
  PRODUCT_NAME "NSA Chords"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth)
target_sources(NSAChords PRIVATE Plugin.cpp)
target_compile_definitions(NSAChords PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(NSAChords PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
