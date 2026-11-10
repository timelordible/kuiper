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

void OscServer::pushDirection (int index)
{
    const auto& p = positions[(size_t) index];
    const auto d = kuiper::directionFromAzEl (p.azimuth, p.elevation);
    auto& source = renderer.source (index);
    source.x.store (d.x);
    source.y.store (d.y);
    source.z.store (d.z);
}

void OscServer::oscMessageReceived (const juce::OSCMessage& m)
{
    const auto address = m.getAddressPattern().toString();

    if (address == "/master/gain")
    {
        float db;
        if (number (m, 0, db))
            renderer.setMasterGain (juce::Decibels::decibelsToGain (db, -120.0f));
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
    auto& source = renderer.source (index);
    float a, b, c;

    if (address == "/source/azim" && number (m, 1, a))
    {
        pos.azimuth = a;
        pushDirection (index);
    }
    else if (address == "/source/elev" && number (m, 1, a))
    {
        pos.elevation = a;
        pushDirection (index);
    }
    else if (address == "/source/dist" && number (m, 1, a))
    {
        pos.distance = a;  // TODO: distance model (see open questions)
    }
    else if (address == "/source/aed" && number (m, 1, a) && number (m, 2, b) && number (m, 3, c))
    {
        pos = { a, b, c };
        pushDirection (index);
    }
    else if (address == "/source/xyz" && number (m, 1, a) && number (m, 2, b) && number (m, 3, c))
    {
        // Keep the spherical copy in step so later azim/elev messages start from here
        pos.distance = std::sqrt (a * a + b * b + c * c);
        if (pos.distance > 1.0e-6f)
        {
            pos.azimuth = std::atan2 (b, a);
            pos.elevation = std::asin (c / pos.distance);
            source.x.store (a);
            source.y.store (b);
            source.z.store (c);
        }
    }
    else if (address == "/source/gain" && number (m, 1, a))
    {
        source.gain.store (juce::Decibels::decibelsToGain (a, -120.0f));
    }
    else if (address == "/source/mute" && number (m, 1, a))
    {
        source.mute.store (a != 0.0f);
    }
}
