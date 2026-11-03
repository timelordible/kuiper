// Gain checks from the outline's testing section. Plain asserts, no framework.

#include "Vbap.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace kuiper;

namespace
{
    constexpr float pi = 3.14159265358979f;
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            std::printf ("FAIL: %s\n", what.c_str());
            ++failures;
        }
    }

    // 8 speakers at ear height, 4 above at 45 deg, an imaginary speaker straight down
    std::vector<Speaker> domeLayout()
    {
        std::vector<Speaker> layout;
        for (int i = 0; i < 8; ++i)
            layout.push_back ({ (float) (i * 45), 0.0f, false, i });
        for (int i = 0; i < 4; ++i)
            layout.push_back ({ 45.0f + (float) (i * 90), 45.0f, false, 8 + i });
        layout.push_back ({ 0.0f, -90.0f, true, -1 });
        return layout;
    }

    std::vector<Speaker> ringLayout()
    {
        std::vector<Speaker> layout;
        for (int i = 0; i < 8; ++i)
            layout.push_back ({ (float) (i * 45), 0.0f, false, i });
        return layout;
    }

    Vbap prepared (const std::vector<Speaker>& layout)
    {
        Vbap vbap;
        std::string error;
        check (vbap.prepare (layout, error), "prepare: " + error);
        return vbap;
    }

    Vec3 degrees (float az, float el) { return directionFromAzEl (az * pi / 180.0f, el * pi / 180.0f); }

    void sourceOnSpeakerPlaysOnlyThere (const std::vector<Speaker>& layout, const char* name)
    {
        const auto vbap = prepared (layout);
        std::vector<float> g ((size_t) vbap.numSpeakers());

        for (size_t s = 0; s < layout.size(); ++s)
        {
            vbap.computeGains (degrees (layout[s].azimuthDeg, layout[s].elevationDeg), g.data());

            for (size_t k = 0; k < g.size(); ++k)
            {
                const auto expected = k == s ? 1.0f : 0.0f;
                check (std::abs (g[k] - expected) < 1.0e-3f,
                       std::string (name) + ": source on speaker " + std::to_string (s)
                           + " gives gain " + std::to_string (g[k]) + " on speaker " + std::to_string (k));
            }
        }
    }

    void powerIsConstant (const std::vector<Speaker>& layout, const char* name)
    {
        const auto vbap = prepared (layout);
        std::vector<float> g ((size_t) vbap.numSpeakers());

        for (int el = -90; el <= 90; el += 5)
            for (int az = -180; az < 180; az += 5)
            {
                vbap.computeGains (degrees ((float) az, (float) el), g.data());
                float power = 0.0f;
                for (auto v : g)
                {
                    check (v >= 0.0f, std::string (name) + ": negative gain");
                    power += v * v;
                }
                check (std::abs (power - 1.0f) < 1.0e-3f,
                       std::string (name) + ": power " + std::to_string (power) + " at az "
                           + std::to_string (az) + " el " + std::to_string (el));
            }
    }

    void sweepHasNoJumps (const std::vector<Speaker>& layout, float elevation, const char* name)
    {
        const auto vbap = prepared (layout);
        std::vector<float> previous ((size_t) vbap.numSpeakers()), g (previous.size());
        vbap.computeGains (degrees (0.0f, elevation), previous.data());

        float worst = 0.0f;
        for (int step = 1; step <= 3600; ++step)
        {
            vbap.computeGains (degrees ((float) step * 0.1f, elevation), g.data());
            for (size_t k = 0; k < g.size(); ++k)
                worst = std::max (worst, std::abs (g[k] - previous[k]));
            previous = g;
        }

        check (worst < 0.02f, std::string (name) + ": gain jumps by " + std::to_string (worst) + " in a 0.1 deg step");
    }

    void halfwayIsEqualPower()
    {
        const auto vbap = prepared (ringLayout());
        std::vector<float> g ((size_t) vbap.numSpeakers());
        vbap.computeGains (degrees (22.5f, 0.0f), g.data());

        const auto half = 1.0f / std::sqrt (2.0f);
        check (std::abs (g[0] - half) < 1.0e-3f && std::abs (g[1] - half) < 1.0e-3f, "ring: 22.5 deg is not equal power");
    }

    void positiveAzimuthIsLeft()
    {
        const auto vbap = prepared (ringLayout());
        std::vector<float> g ((size_t) vbap.numSpeakers());
        vbap.computeGains (directionFromAzEl (pi / 2.0f, 0.0f), g.data());
        check (g[2] > 0.999f, "ring: +90 deg azimuth should play from the speaker at 90 deg (left)");
    }
}

int main()
{
    const auto dome = domeLayout();
    const auto ring = ringLayout();

    check (prepared (dome).numGroups() > 0, "dome: no triangles");
    check (prepared (ring).is2D(), "ring: should use 2D VBAP");

    sourceOnSpeakerPlaysOnlyThere (dome, "dome");
    sourceOnSpeakerPlaysOnlyThere (ring, "ring");
    powerIsConstant (dome, "dome");
    sweepHasNoJumps (dome, 0.0f, "dome at 0 deg");
    sweepHasNoJumps (dome, 20.0f, "dome at 20 deg");
    sweepHasNoJumps (ring, 0.0f, "ring");
    halfwayIsEqualPower();
    positiveAzimuthIsLeft();

    if (failures == 0)
        std::printf ("All VBAP tests passed\n");

    return failures == 0 ? 0 : 1;
}
