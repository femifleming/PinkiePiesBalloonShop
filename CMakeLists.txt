cmake_minimum_required(VERSION 3.22)

# macOS: one universal build for Apple silicon and Intel
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")

project(PinkiePiesBalloonShop VERSION 1.0.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BALLOONSHOP_BUILD_TESTS "Build the offline and host tests" OFF)
option(BALLOONSHOP_COPY_AFTER_BUILD "Install the plug-ins into your user plug-in folders after building" ON)

add_subdirectory(JUCE)

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux
if(APPLE)
    set(BS_FORMATS AU VST3 Standalone)
elseif(WIN32)
    set(BS_FORMATS VST3 Standalone)
else()
    set(BS_FORMATS VST3 LV2 Standalone)
endif()

juce_add_plugin(BalloonShop
    VERSION 1.0.3
    COMPANY_NAME "FreakiePie"
    COMPANY_WEBSITE "https://github.com/femifleming/PinkiePiesBalloonShop"
    BUNDLE_ID "com.freakiepie.freakiepie"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Fkpi
    FORMATS ${BS_FORMATS}
    PRODUCT_NAME "Pinkie Pies Balloon Shop"          # the file name (an apostrophe breaks the VST3 build step)
    PLUGIN_NAME "Pinkie Pie's Balloon Shop"          # what your DAW shows
    DESCRIPTION "A multi-effect played as balloons"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    LV2URI "https://github.com/femifleming/PinkiePiesBalloonShop"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD ${BALLOONSHOP_COPY_AFTER_BUILD})

target_sources(BalloonShop PRIVATE
    Source/PluginProcessor.cpp
    Source/Presets.cpp
    Source/PluginEditor.cpp)

set(BS_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_compile_definitions(BalloonShop PUBLIC ${BS_DEFS})

file(GLOB BS_BACKGROUNDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/bg_*.jpg)
juce_add_binary_data(BalloonShopAssets SOURCES
    Resources/pinkie_sitting.png Resources/pinkie_umbrella.png Resources/pinkie_crystal.png Resources/cheerilee.png
    Resources/pinkie_cower.png Resources/pinkie_scared.png Resources/pinkie_sleep.png
    Resources/pinkie_snow.png Resources/pinkie_cold.png Resources/pinkie_heat.png Resources/pinkie_wet.png
    Resources/library.jpg Resources/twilight_reading.png Resources/trixie.png Resources/mlp_magic_is_friendship.ttf
    ${BS_BACKGROUNDS})

target_link_libraries(BalloonShop
    PRIVATE
        BalloonShopAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# tests (not part of the plug-in): cmake -B build -DBALLOONSHOP_BUILD_TESTS=ON
#==============================================================================
if(BALLOONSHOP_BUILD_TESTS)
    juce_add_console_app(BalloonShopTest PRODUCT_NAME "BalloonShopTest")
    target_sources(BalloonShopTest PRIVATE tests/OfflineTest.cpp)
    target_compile_definitions(BalloonShopTest PRIVATE ${BS_DEFS})
    target_link_libraries(BalloonShopTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

    juce_add_gui_app(BalloonShopHostTest PRODUCT_NAME "BalloonShopHostTest")
    target_sources(BalloonShopHostTest PRIVATE tests/HostTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Presets.cpp)
    target_compile_definitions(BalloonShopHostTest PRIVATE ${BS_DEFS})
    target_link_libraries(BalloonShopHostTest PRIVATE BalloonShopAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
