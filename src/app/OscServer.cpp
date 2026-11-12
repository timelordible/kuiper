#include "OscServer.h"

#include <cmath>
#include <iostream>

namespace
{
    // Senders differ on int vs float, so accept either
    bool number (const juce::OSCMessage& m, int i, float& value)
    {
        if (i >= m.size())
            return false;
        if (m[i].isFloat32()) { value = m[i].getFloat32(); return true; }
        if (m[i].isInt32())   { value = (float) m[i].getInt32(); return true; }
        return false;
    }
}

OscServer::OscServer (Renderer& r)
    : renderer (r), positions ((size_t) r.numSources())
{
}

OscServer::~OscServer()
{
    receiver.removeListener (this);
    receiver.disconnect();
}

bool OscServer::start (int port)
{
    if (! receiver.connect (port))
        return false;

    receiver.addListener (this);
    return true;
}

void OscServer::post (const ControlMessage& message)
{
    if (renderer.post (message, juce::Time::getMillisecondCounterHiRes()))
        return;

    // The audio thread isn't keeping up; report the first drop and then every 1000th
    if (droppedMessages++ % 1000 == 0)
        std::cerr << "Control queue full, dropped " << droppedMessages << " message(s)" << std::endl;
}

void OscServer::postDirection (int index)
{
    const auto& p = positions[(size_t) index];
    post ({ ControlMessage::Type::direction, index, kuiper::directionFromAzEl (p.azimuth, p.elevation) });
}

void OscServer::oscMessageReceived (const juce::OSCMessage& m)
{
    const auto address = m.getAddressPattern().toString();

    if (address == "/master/gain")
    {
        float db;
        if (number (m, 0, db))
            post ({ ControlMessage::Type::masterGain, 0, {}, juce::Decibels::decibelsToGain (db, -120.0f) });
        return;
    }

    if (! address.startsWith ("/source/"))
        return;

    float indexValue;
    if (! number (m, 0, indexValue))
        return;

    const auto index = (int) indexValue;
    if (index < 0 || index >= renderer.numSources())
    {
        std::cerr << "Ignoring " << address << " for source " << index << " (out of range)" << std::endl;
        return;
    }

    auto& pos = positions[(size_t) index];
    float a, b, c;

    if (address == "/source/azim" && number (m, 1, a))
    {
        pos.azimuth = a;
        postDirection (index);
    }
    else if (address == "/source/elev" && number (m, 1, a))
    {
        pos.elevation = a;
        postDirection (index);
    }
    else if (address == "/source/dist" && number (m, 1, a))
    {
        pos.distance = a;  // TODO: distance model (see open questions)
    }
    else if (address == "/source/aed" && number (m, 1, a) && number (m, 2, b) && number (m, 3, c))
    {
        pos = { a, b, c };
        postDirection (index);
    }
    else if (address == "/source/xyz" && number (m, 1, a) && number (m, 2, b) && number (m, 3, c))
    {
        // Keep the spherical copy in step so later azim/elev messages start from here
        pos.distance = std::sqrt (a * a + b * b + c * c);
        if (pos.distance > 1.0e-6f)
        {
            pos.azimuth = std::atan2 (b, a);
            pos.elevation = std::asin (c / pos.distance);
            post ({ ControlMessage::Type::direction, index, { a, b, c } });
        }
    }
    else if (address == "/source/gain" && number (m, 1, a))
    {
        post ({ ControlMessage::Type::gain, index, {}, juce::Decibels::decibelsToGain (a, -120.0f) });
    }
    else if (address == "/source/mute" && number (m, 1, a))
    {
        post ({ ControlMessage::Type::mute, index, {}, a != 0.0f ? 1.0f : 0.0f });
    }
}
