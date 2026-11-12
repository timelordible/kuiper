#pragma once

#include "Vbap.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <vector>

// A change for the audio thread, posted by a control thread (OSC, later the web server).
struct ControlMessage
{
    enum class Type { direction, gain, mute, masterGain };

    Type type = Type::direction;
    int source = 0;
    kuiper::Vec3 direction;  // direction: x front, y left, z up, need not be unit length
    float value = 0.0f;      // gain and masterGain: linear; mute: 0 or 1
};

// The audio callback: each input is panned with VBAP straight to the speaker outputs.
//
// Control runs in fixed frames of controlFrameSize samples, whatever the device's buffer
// size. Messages are stamped with their arrival time and replayed one buffer late, spread
// across the buffer the way they arrived during the previous one, so fast OSC updates
// keep their timing. Each frame smooths every source's position, recomputes its VBAP
// gains and ramps to them over the frame.
class Renderer : public juce::AudioIODeviceCallback
{
public:
    static constexpr int controlFrameSize = 64;
    static constexpr int queueCapacity = 4096;

    Renderer (int numSources, std::vector<kuiper::Speaker> speakers, const kuiper::Vbap& vbap);

    int numSources() const { return sourceCount; }

    // Call from ONE control thread only: the queue has a single producer.
    // arrivalMs is juce::Time::getMillisecondCounterHiRes() when the message arrived.
    // Returns false if the queue is full and the message was dropped.
    bool post (const ControlMessage& message, double arrivalMs);

    // Set before audio starts. Changes while running go through post().
    void setMasterGain (float linear) { masterGain = linear; }
    void setSmoothingMs (float ms) { smoothingMs.store (juce::jmax (0.0f, ms)); }

    // Highest output channel any real speaker uses, + 1
    int numOutputChannelsNeeded() const;

    void prepare (double newSampleRate) { sampleRate = newSampleRate; }

    // The audio work. callbackStartMs is the time the callback began, on the same
    // clock as the arrival times passed to post(); tests pass their own times.
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs,
                  int numSamples, double callbackStartMs);

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

private:
    struct QueuedMessage
    {
        ControlMessage message;
        double arrivalMs = 0.0;
    };

    struct SourceState
    {
        kuiper::Vec3 target { 1.0f, 0.0f, 0.0f };
        kuiper::Vec3 smoothed { 1.0f, 0.0f, 0.0f };
        float gain = 1.0f;
        bool mute = false;
    };

    void applyMessagesDueBy (double timeMs);
    void apply (const ControlMessage& message);

    const int sourceCount;
    const std::vector<kuiper::Speaker> speakers;
    const kuiper::Vbap& vbap;

    juce::AbstractFifo fifo { queueCapacity };
    std::vector<QueuedMessage> queue;

    std::atomic<float> smoothingMs { 20.0f };
    double sampleRate = 48000.0;

    // Audio-thread state, allocated once in the constructor
    float masterGain = 0.75f;
    double previousCallbackMs = -1.0;
    std::vector<SourceState> sources;
    std::vector<float> currentGains;  // sources x speakers, the gains reached at the end of the last frame
    std::vector<float> targetGains;   // speakers, scratch
};
