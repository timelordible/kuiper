#pragma once

#include "Vbap.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>
#include <vector>

// Per-source parameters, written by the control threads (OSC, later the web
// server) and read by the audio thread. Atomics only, so nothing ever blocks.
struct SourceParams
{
    std::atomic<float> x { 1.0f }, y { 0.0f }, z { 0.0f };  // target direction, need not be unit length
    std::atomic<float> gain { 1.0f };                       // linear
    std::atomic<bool> mute { false };
};

// The audio callback: each input is panned with VBAP straight to the speaker outputs.
class Renderer : public juce::AudioIODeviceCallback
{
public:
    Renderer (int numSources, std::vector<kuiper::Speaker> speakers, const kuiper::Vbap& vbap);

    int numSources() const { return sourceCount; }
    SourceParams& source (int index) { return params[(size_t) index]; }

    void setMasterGain (float linear) { masterGain.store (linear); }
    void setSmoothingMs (float ms) { smoothingMs.store (juce::jmax (0.0f, ms)); }

    // Highest output channel any real speaker uses, + 1
    int numOutputChannelsNeeded() const;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

private:
    const int sourceCount;
    const std::vector<kuiper::Speaker> speakers;
    const kuiper::Vbap& vbap;

    std::unique_ptr<SourceParams[]> params;
    std::atomic<float> masterGain { 0.75f };
    std::atomic<float> smoothingMs { 20.0f };
    double sampleRate = 48000.0;

    // Audio-thread state, allocated once in the constructor
    std::vector<kuiper::Vec3> smoothedDirection;  // per source
    std::vector<float> currentGains;              // sources x speakers, the gains reached at the end of the last block
    std::vector<float> targetGains;               // speakers, scratch
};
