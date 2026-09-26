cmake_minimum_required(VERSION 3.22)

# ---- Platform setup (must happen before project()) --------------------------
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
    endif()
endif()

# Static MSVC runtime: users never need to install the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(OJU VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OJU_BUILD_TESTS "Build the headless OJU test runner" OFF)
set(OJU_V1_SOURCE_DIR "" CACHE PATH "Checkout of OJU v1 (for the v1/v2 null test renderer)")

# ---- JUCE (pinned) ----------------------------------------------------------
# To build offline, pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

# ---- Plugin -----------------------------------------------------------------
if(APPLE)
    set(OJU_FORMATS VST3 AU Standalone)
else()
    set(OJU_FORMATS VST3 Standalone)
endif()

juce_add_plugin(OJU
    PRODUCT_NAME                "OJU"
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "Made by Joseph"
    COMPANY_WEBSITE             "https://madebyjoseph.com"
    COMPANY_COPYRIGHT           "Copyright (c) Made by Joseph"
    BUNDLE_ID                   com.madebyjoseph.oju
    PLUGIN_MANUFACTURER_CODE    Mbyj
    PLUGIN_CODE                 Ojue
    DESCRIPTION                 "Self-setting vocal chain"
    FORMATS                     ${OJU_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AU_SANDBOX_SAFE             TRUE
    HARDENED_RUNTIME_ENABLED    TRUE
    HARDENED_RUNTIME_OPTIONS    com.apple.security.device.audio-input
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "OJU listens to your microphone so it can set itself up."
    COPY_PLUGIN_AFTER_BUILD     FALSE)

set(OJU_SOURCES
    Source/PluginProcessor.cpp
    Source/Parameters.cpp
    Source/dsp/VocalChain.cpp
    Source/dsp/Engine.cpp
    Source/brain/Listener.cpp
    Source/brain/Brain.cpp
    Source/ui/Theme.cpp
    Source/ui/OjuLookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/EyeButton.cpp
    Source/ui/EqCurveView.cpp
    Source/ui/Backplate.cpp
    Source/ui/Rack.cpp
    Source/ui/ModuleViews.cpp
    Source/ui/PluginEditor.cpp)

target_sources(OJU PRIVATE ${OJU_SOURCES})
target_include_directories(OJU PRIVATE Source)

set(OJU_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(UNIX AND NOT APPLE)
    list(APPEND OJU_DEFINITIONS JUCE_JACK=1)   # Linux dev builds: JACK + ALSA in the standalone
endif()

target_compile_definitions(OJU PUBLIC ${OJU_DEFINITIONS})

set(OJU_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics)

target_link_libraries(OJU
    PRIVATE
        ${OJU_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Headless tests (DSP, brain, editor snapshot) --------------------------
if(OJU_BUILD_TESTS)
    juce_add_console_app(OJUTests PRODUCT_NAME "OJUTests")
    target_sources(OJUTests PRIVATE Tests/OjuTests.cpp ${OJU_SOURCES})
    target_include_directories(OJUTests PRIVATE Source)
    target_compile_definitions(OJUTests PRIVATE ${OJU_DEFINITIONS} OJU_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(OJUTests
        PRIVATE
            ${OJU_MODULES}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---- Offline renderers for the v1 <-> v2 null test ---------------------------
if(OJU_BUILD_TESTS)
    juce_add_console_app(OJURender PRODUCT_NAME "OJURender")
    target_sources(OJURender PRIVATE Tests/Render.cpp ${OJU_SOURCES})
    target_include_directories(OJURender PRIVATE Source)
    target_compile_definitions(OJURender PRIVATE ${OJU_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(OJURender PRIVATE ${OJU_MODULES} ${OJU_EXTRA_LIBS}
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    if(OJU_V1_SOURCE_DIR)
        # Frozen v1 sources, compiled with exactly the same flags as v2.
        file(GLOB_RECURSE OJU_V1_SOURCES CONFIGURE_DEPENDS "${OJU_V1_SOURCE_DIR}/Source/*.cpp")
        juce_add_console_app(OJURenderV1 PRODUCT_NAME "OJURenderV1")
        target_sources(OJURenderV1 PRIVATE Tests/Render.cpp ${OJU_V1_SOURCES})
        target_include_directories(OJURenderV1 PRIVATE "${OJU_V1_SOURCE_DIR}/Source")
        target_compile_definitions(OJURenderV1 PRIVATE ${OJU_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(OJURenderV1 PRIVATE ${OJU_MODULES}
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endif()
endif()
