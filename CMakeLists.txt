cmake_minimum_required(VERSION 3.22)
project(Kuiper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build only the JUCE-free VBAP core and its tests with -DKUIPER_BUILD_APP=OFF
option(KUIPER_BUILD_APP "Build the JUCE renderer app (downloads JUCE)" ON)

# ---------------------------------------------------------------------------
# VBAP core: plain C++, no JUCE, so it can be tested on its own
# ---------------------------------------------------------------------------
add_library(kuiper_core STATIC src/core/Vbap.cpp)
target_include_directories(kuiper_core PUBLIC src/core)

enable_testing()
add_executable(vbap_tests tests/vbap_tests.cpp)
target_link_libraries(vbap_tests PRIVATE kuiper_core)
add_test(NAME vbap_tests COMMAND vbap_tests)

# ---------------------------------------------------------------------------
# Renderer app: headless JUCE console app
# ---------------------------------------------------------------------------
if(KUIPER_BUILD_APP)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)

    juce_add_console_app(kuiper PRODUCT_NAME "kuiper")

    target_sources(kuiper PRIVATE
        src/app/Main.cpp
        src/app/Config.cpp
        src/app/Layout.cpp
        src/app/Renderer.cpp
        src/app/OscServer.cpp)

    target_compile_definitions(kuiper PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    if(UNIX AND NOT APPLE)
        target_compile_definitions(kuiper PRIVATE
            JUCE_JACK=1
            JUCE_ALSA=1
            "JUCE_JACK_CLIENT_NAME=\"kuiper\"")
    endif()

    target_link_libraries(kuiper
        PRIVATE
            kuiper_core
            juce::juce_core
            juce::juce_events
            juce::juce_audio_basics
            juce::juce_audio_devices
            juce::juce_osc
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
