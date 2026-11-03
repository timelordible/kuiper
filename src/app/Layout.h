#pragma once

#include "Vbap.h"

#include <juce_core/juce_core.h>

// Reads an IEM speaker layout (.json, as exported by AllRADecoder).
// Channel numbers in the file are 1-based; they become 0-based output channels.
bool loadIemLayout (const juce::File& file, std::vector<kuiper::Speaker>& speakers, juce::String& error);
