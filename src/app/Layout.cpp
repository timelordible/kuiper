#include "Layout.h"

bool loadIemLayout (const juce::File& file, std::vector<kuiper::Speaker>& speakers, juce::String& error)
{
    speakers.clear();

    if (! file.existsAsFile())
    {
        error = "layout file not found: " + file.getFullPathName();
        return false;
    }

    juce::var root;
    const auto result = juce::JSON::parse (file.loadFileAsString(), root);
    if (result.failed())
    {
        error = "could not parse " + file.getFullPathName() + ": " + result.getErrorMessage();
        return false;
    }

    const auto* list = root["LoudspeakerLayout"]["Loudspeakers"].getArray();
    if (list == nullptr)
    {
        error = "no LoudspeakerLayout/Loudspeakers array in " + file.getFileName();
        return false;
    }

    for (const auto& entry : *list)
    {
        kuiper::Speaker speaker;
        speaker.azimuthDeg = (float) entry["Azimuth"];
        speaker.elevationDeg = (float) entry["Elevation"];
        speaker.imaginary = (bool) entry["IsImaginary"];
        speaker.outputChannel = speaker.imaginary ? -1 : (int) entry["Channel"] - 1;

        if (! speaker.imaginary && speaker.outputChannel < 0)
        {
            error = "speaker at azimuth " + juce::String (speaker.azimuthDeg) + " has no channel";
            return false;
        }

        speakers.push_back (speaker);
    }

    return true;
}
