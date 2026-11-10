#include "Config.h"

bool loadConfig (const juce::File& file, Config& config, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "config file not found: " + file.getFullPathName();
        return false;
    }

    juce::var root;
    const auto result = juce::JSON::parse (file.loadFileAsString(), root);
    if (result.failed() || ! root.isObject())
    {
        error = "could not parse " + file.getFullPathName() + ": " + result.getErrorMessage();
        return false;
    }

    const auto layout = root["layout"].toString();
    if (layout.isEmpty())
    {
        error = "config has no \"layout\"";
        return false;
    }

    config.layoutFile = file.getParentDirectory().getChildFile (layout);

    if (root.hasProperty ("inputs"))
        config.numInputs = juce::jlimit (1, 256, (int) root["inputs"]);

    if (const auto& audio = root["audio"]; audio.isObject())
    {
        config.deviceType = audio["type"].toString();
        config.inputDevice = audio["inputDevice"].toString();
        config.outputDevice = audio["outputDevice"].toString();
        if (audio.hasProperty ("sampleRate")) config.sampleRate = (double) audio["sampleRate"];
        if (audio.hasProperty ("bufferSize")) config.bufferSize = (int) audio["bufferSize"];
    }

    if (const auto& osc = root["osc"]; osc.isObject() && osc.hasProperty ("port"))
        config.oscPort = (int) osc["port"];

    if (root.hasProperty ("smoothingMs")) config.smoothingMs = (float) root["smoothingMs"];
    if (root.hasProperty ("masterGainDb")) config.masterGainDb = (float) root["masterGainDb"];

    return true;
}
