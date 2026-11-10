#pragma once

#include "Renderer.h"

#include <juce_osc/juce_osc.h>

// OSC control. Accepts litespat's messages unchanged, plus the new ones in the outline:
//   /source/azim  <int idx> <float rad>      (litespat)
//   /source/elev  <int idx> <float rad>      (litespat)
//   /source/dist  <int idx> <float m>        (litespat; stored, not used yet)
//   /source/aed   <int idx> <az rad> <el rad> <dist m>
//   /source/xyz   <int idx> <x> <y> <z>      (metres; x front, y left, z up)
//   /source/gain  <int idx> <float dB>
//   /source/mute  <int idx> <int 0|1>
//   /master/gain  <float dB>
class OscServer : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    explicit OscServer (Renderer& renderer);
    ~OscServer() override;

    bool start (int port);

private:
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void pushDirection (int index);

    // Spherical coordinates per source, so separate azim and elev messages can be combined.
    // Only touched on the OSC receiver thread.
    struct Aed
    {
        float azimuth = 0.0f, elevation = 0.0f, distance = 1.5f;
    };

    Renderer& renderer;
    juce::OSCReceiver receiver { "kuiper OSC" };
    std::vector<Aed> positions;
};
