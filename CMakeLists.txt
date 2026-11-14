cmake_minimum_required(VERSION 3.25)

project(Breath VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mac: binaire universel (Apple Silicon + Intel), macOS 11 minimum
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# Windows: runtime C++ statique (pas besoin d'installer de redistribuable chez l'utilisateur)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- JUCE, téléchargé automatiquement -------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Le plugin -------------------------------------------------------------
juce_add_plugin(Breath
    COMPANY_NAME                "Breath"
    BUNDLE_ID                   "com.breath.breath"
    PLUGIN_MANUFACTURER_CODE    Brth
    PLUGIN_CODE                 Brt1
    FORMATS                     VST3
    PRODUCT_NAME                "Breath"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
)

target_sources(Breath PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_compile_definitions(Breath PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Breath
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Outil de capture d'écran (développement uniquement) --------------------
option(BREATH_BUILD_TOOLS "Build developer tools (screenshot)" OFF)
if(BREATH_BUILD_TOOLS)
    add_executable(BreathScreenshot tools/Screenshot.cpp)
    # Réutilise les modules JUCE déjà compilés dans la bibliothèque du plugin.
    target_include_directories(BreathScreenshot PRIVATE $<TARGET_PROPERTY:Breath,INCLUDE_DIRECTORIES>)
    target_compile_definitions(BreathScreenshot PRIVATE $<TARGET_PROPERTY:Breath,COMPILE_DEFINITIONS>)
    target_link_libraries(BreathScreenshot PRIVATE Breath)
endif()
