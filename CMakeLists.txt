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
    BUNDLE_ID                   "com.breath.breathone"
    PLUGIN_MANUFACTURER_CODE    Brth
    PLUGIN_CODE                 BrOn
    FORMATS                     VST3
    PRODUCT_NAME                "Breath One"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
)

# Polices (Inter, licence SIL OFL) embarquées dans le plugin
juce_add_binary_data(BreathAssets
    HEADER_NAME BreathAssets.h
    NAMESPACE BreathAssets
    SOURCES
        resources/fonts/Inter-Regular.otf
        resources/fonts/Inter-Medium.otf
        resources/fonts/Inter-SemiBold.otf
)
set_target_properties(BreathAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Breath PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/hub/BreathHub.cpp
    src/dsp/Analyzer.cpp
    src/dsp/FilterBank.cpp
    src/ui/Theme.cpp
    src/ui/Widgets.cpp
    src/ui/TopBar.cpp
    src/ui/SpectrumView.cpp
    src/ui/InstanceView.cpp
    src/ui/MatrixView.cpp
    src/ui/EnsembleView.cpp
    src/ui/TitleBar.cpp
)

target_compile_definitions(Breath PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Breath
    PRIVATE
        BreathAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Outils de développement : tests automatiques et captures d'écran -------
option(BREATH_BUILD_TOOLS "Build developer tools (tests, screenshot)" OFF)
if(BREATH_BUILD_TOOLS)
    # L'outil de capture a besoin d'une boucle de messages (uniquement dans ce build de développement).
    target_compile_definitions(Breath PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)
    foreach(tool BreathTests BreathScreenshot)
        if(tool STREQUAL "BreathTests")
            add_executable(${tool} tests/Tests.cpp)
        else()
            add_executable(${tool} tools/Screenshot.cpp)
        endif()
        # Réutilise les modules JUCE déjà compilés dans la bibliothèque du plugin.
        target_include_directories(${tool} PRIVATE $<TARGET_PROPERTY:Breath,INCLUDE_DIRECTORIES> src)
        target_compile_definitions(${tool} PRIVATE $<TARGET_PROPERTY:Breath,COMPILE_DEFINITIONS>)
        target_link_libraries(${tool} PRIVATE Breath)
    endforeach()
endif()
