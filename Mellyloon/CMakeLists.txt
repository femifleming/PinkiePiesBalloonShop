cmake_minimum_required(VERSION 3.22)

# macOS: one universal build for Apple silicon and Intel
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")

project(Mellyloon VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MELLYLOON_BUILD_TESTS "Build the offline and host tests" OFF)
option(MELLYLOON_COPY_AFTER_BUILD "Install the plug-ins into your user plug-in folders after building" ON)

# Mellyloon lives in the Balloon Shop's repository: it shares the shop's JUCE and its pictures
set(SHOP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT EXISTS "${SHOP_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing: run  git submodule update --init  in the PinkiePiesBalloonShop folder")
endif()
add_subdirectory("${SHOP_DIR}/JUCE" "${CMAKE_BINARY_DIR}/JUCE")

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux
if(APPLE)
    set(SPH_FORMATS AU VST3 Standalone)
elseif(WIN32)
    set(SPH_FORMATS VST3 Standalone)
else()
    set(SPH_FORMATS VST3 LV2 Standalone)
endif()

juce_add_plugin(Mellyloon
    VERSION 0.1.0
    COMPANY_NAME "FreakiePie"
    COMPANY_WEBSITE "https://github.com/femifleming/PinkiePiesBalloonShop/tree/main/Mellyloon"
    BUNDLE_ID "com.freakiepie.mellyloon"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Fkps
    FORMATS ${SPH_FORMATS}
    PRODUCT_NAME "Mellyloon"          # the file name (an apostrophe breaks the VST3 build step)
    PLUGIN_NAME "Mellyloon"          # what your DAW shows
    DESCRIPTION "A spatial multi-effect played as balloons in a sphere (3rd-order Ambisonics, binaural, stereo)"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    LV2URI "https://github.com/femifleming/PinkiePiesBalloonShop/Mellyloon"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Spatial
    COPY_PLUGIN_AFTER_BUILD ${MELLYLOON_COPY_AFTER_BUILD})

target_sources(Mellyloon PRIVATE
    Source/PluginProcessor.cpp
    Source/Presets.cpp
    Source/PluginEditor.cpp)

set(SPH_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_compile_definitions(Mellyloon PUBLIC ${SPH_DEFS})

file(GLOB SPH_BACKGROUNDS ${SHOP_DIR}/Resources/bg_*.jpg)
juce_add_binary_data(MellyloonAssets SOURCES
    ${SHOP_DIR}/Resources/pinkie_sitting.png ${SHOP_DIR}/Resources/pinkie_umbrella.png ${SHOP_DIR}/Resources/pinkie_crystal.png ${SHOP_DIR}/Resources/cheerilee.png
    ${SHOP_DIR}/Resources/pinkie_cower.png ${SHOP_DIR}/Resources/pinkie_scared.png ${SHOP_DIR}/Resources/pinkie_sleep.png
    ${SHOP_DIR}/Resources/pinkie_snow.png ${SHOP_DIR}/Resources/pinkie_cold.png ${SHOP_DIR}/Resources/pinkie_heat.png ${SHOP_DIR}/Resources/pinkie_wet.png
    ${SHOP_DIR}/Resources/library.jpg ${SHOP_DIR}/Resources/twilight_reading.png ${SHOP_DIR}/Resources/trixie.png ${SHOP_DIR}/Resources/rainbow_dash.png ${SHOP_DIR}/Resources/pinkie_wow.png ${SHOP_DIR}/Resources/mlp_magic_is_friendship.ttf
    ${SPH_BACKGROUNDS})

target_link_libraries(Mellyloon
    PRIVATE
        MellyloonAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# tests (not part of the plug-in): cmake -B build -DMELLYLOON_BUILD_TESTS=ON
#==============================================================================
if(MELLYLOON_BUILD_TESTS)
    juce_add_console_app(MellyloonTest PRODUCT_NAME "MellyloonTest")
    target_sources(MellyloonTest PRIVATE tests/OfflineTest.cpp)
    target_compile_definitions(MellyloonTest PRIVATE ${SPH_DEFS})
    target_link_libraries(MellyloonTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(MellyloonSphereTest PRODUCT_NAME "MellyloonSphereTest")
    target_sources(MellyloonSphereTest PRIVATE tests/SphereTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Presets.cpp)
    target_compile_definitions(MellyloonSphereTest PRIVATE ${SPH_DEFS})
    target_link_libraries(MellyloonSphereTest PRIVATE MellyloonAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

    juce_add_gui_app(MellyloonHostTest PRODUCT_NAME "MellyloonHostTest")
    target_sources(MellyloonHostTest PRIVATE tests/HostTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Presets.cpp)
    target_compile_definitions(MellyloonHostTest PRIVATE ${SPH_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MellyloonHostTest PRIVATE MellyloonAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
    # (Windows: M_PI for the tests, and a bigger stack: they make whole plug-ins as local variables)
    if(MSVC)
        foreach(t MellyloonTest MellyloonSphereTest MellyloonHostTest)
            target_compile_definitions(${t} PRIVATE _USE_MATH_DEFINES)
            target_link_options(${t} PRIVATE /STACK:16777216)
            target_compile_options(${t} PRIVATE /bigobj)
        endforeach()
    endif()
endif()
