if(DEFINED JUCE_ROOT AND NOT JUCE_ROOT STREQUAL "")
    add_subdirectory(${JUCE_ROOT} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(AmpNeveVST
    COMPANY_NAME "AmpNeve"
    VERSION 1.0.0
    PLUGIN_MANUFACTURER_CODE AmNv
    PLUGIN_CODE AmNv
    PRODUCT_NAME "AmpNeve"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)
juce_generate_juce_header(AmpNeveVST)
target_sources(AmpNeveVST PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_include_directories(AmpNeveVST PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(AmpNeveVST PRIVATE ampneve_core)
target_compile_definitions(AmpNeveVST PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_definitions(AmpNeveVST_VST3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
