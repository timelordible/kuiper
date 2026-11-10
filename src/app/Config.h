#pragma once

#include <juce_core/juce_core.h>

// Everything the renderer needs to start without anyone at a screen: one JSON file per room.
struct Config
{
    juce::File layoutFile;            // IEM .json layout, relative paths resolve next to the config
    int numInputs = 32;

    juce::String deviceType;          // "JACK", "ALSA", "CoreAudio", "Windows Audio", "ASIO"; empty = default
    juce::String inputDevice;         // empty = the type's default device
    juce::String outputDevice;
    double sampleRate = 48000.0;
    int bufferSize = 256;

    int oscPort = 6666;
    float smoothingMs = 20.0f;
    float masterGainDb = -2.5f;       // litespat's fixed 0.75
};

bool loadConfig (const juce::File& file, Config& config, juce::String& error);
