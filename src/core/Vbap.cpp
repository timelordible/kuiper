#include "Vbap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kuiper
{

namespace
{
    constexpr float pi = 3.14159265358979f;

    Vec3 sub (Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    float dot (Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Vec3 cross (Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    float length (Vec3 a) { return std::sqrt (dot (a, a)); }

    Vec3 normalised (Vec3 a)
    {
        const auto len = length (a);
        return len > 1.0e-9f ? Vec3 { a.x / len, a.y / len, a.z / len } : Vec3 { 1.0f, 0.0f, 0.0f };
    }

    // Inverse of the 3x3 matrix whose rows are a, b, c. Returns false if singular.
    bool invert3 (Vec3 a, Vec3 b, Vec3 c, std::array<float, 9>& inv)
    {
        const auto det = dot (a, cross (b, c));
        if (std::abs (det) < 1.0e-6f)
            return false;

        // Columns of the inverse are the cross products divided by the determinant
        const auto bc = cross (b, c), ca = cross (c, a), ab = cross (a, b);
        inv = { bc.x / det, ca.x / det, ab.x / det,
                bc.y / det, ca.y / det, ab.y / det,
                bc.z / det, ca.z / det, ab.z / det };
        return true;
    }

    // g = p * inv for a row vector p
    void applyInverse (const std::array<float, 9>& inv, Vec3 p, float* g)
    {
        for (int k = 0; k < 3; ++k)
            g[k] = p.x * inv[(size_t) k] + p.y * inv[(size_t) (3 + k)] + p.z * inv[(size_t) (6 + k)];
    }
}

Vec3 directionFromAzEl (float azimuthRad, float elevationRad)
{
    const auto c = std::cos (elevationRad);
    return { c * std::cos (azimuthRad), c * std::sin (azimuthRad), std::sin (elevationRad) };
}

bool Vbap::prepare (const std::vector<Speaker>& speakers, std::string& error)
{
    dirs.clear();
    groups.clear();

    for (const auto& s : speakers)
        dirs.push_back (directionFromAzEl (s.azimuthDeg * pi / 180.0f, s.elevationDeg * pi / 180.0f));

    if (dirs.size() < 3)
    {
        error = "need at least 3 speakers";
        return false;
    }

    twoD = std::all_of (dirs.begin(), dirs.end(), [] (Vec3 d) { return std::abs (d.z) < 1.0e-3f; });
    return twoD ? pair2D (error) : triangulate3D (error);
}

bool Vbap::triangulate3D (std::string& error)
{
    // Brute-force convex hull: a triple is a face when every other speaker lies on
    // one side of its plane. O(n^4), which is fine for layouts of up to ~64 speakers.
    constexpr float planeEps = 1.0e-4f;
    const auto n = (int) dirs.size();

    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int k = j + 1; k < n; ++k)
            {
                auto normal = cross (sub (dirs[(size_t) j], dirs[(size_t) i]), sub (dirs[(size_t) k], dirs[(size_t) i]));
                if (length (normal) < 1.0e-6f)
                    continue;

                normal = normalised (normal);
                const auto offset = dot (normal, dirs[(size_t) i]);

                // A face through the origin (e.g. the floor of a hemisphere layout) can't pan
                if (std::abs (offset) < 1.0e-3f)
                    continue;

                Group group;
                group.idx = { i, j, k };
                if (! invert3 (dirs[(size_t) i], dirs[(size_t) j], dirs[(size_t) k], group.inv))
                    continue;

                bool above = false, below = false, containsSpeaker = false;

                for (int m = 0; m < n && ! containsSpeaker && ! (above && below); ++m)
                {
                    if (m == i || m == j || m == k)
                        continue;

                    const auto side = dot (normal, dirs[(size_t) m]) - offset;
                    above = above || side > planeEps;
                    below = below || side < -planeEps;

                    // Skip a big triangle that has another coplanar speaker inside it,
                    // so the smaller triangles around that speaker are used instead
                    if (std::abs (side) <= planeEps)
                    {
                        float g[3];
                        applyInverse (group.inv, dirs[(size_t) m], g);
                        containsSpeaker = g[0] > 1.0e-4f && g[1] > 1.0e-4f && g[2] > 1.0e-4f;
                    }
                }

                if (! (above && below) && ! containsSpeaker)
                    groups.push_back (group);
            }

    if (groups.empty())
    {
        error = "could not triangulate the layout";
        return false;
    }

    return true;
}

bool Vbap::pair2D (std::string& error)
{
    std::vector<int> order ((size_t) dirs.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;

    auto azimuth = [this] (int i) { return std::atan2 (dirs[(size_t) i].y, dirs[(size_t) i].x); };
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return azimuth (a) < azimuth (b); });

    for (size_t p = 0; p < order.size(); ++p)
    {
        const auto a = order[p], b = order[(p + 1) % order.size()];
        auto gap = azimuth (b) - azimuth (a);
        if (gap <= 0.0f)
            gap += 2.0f * pi;

        // Pairs 180 deg or more apart can't pan between them
        if (gap >= pi - 1.0e-3f)
            continue;

        const auto& la = dirs[(size_t) a];
        const auto& lb = dirs[(size_t) b];
        const auto det = la.x * lb.y - la.y * lb.x;

        Group group;
        group.size = 2;
        group.idx = { a, b, -1 };
        group.inv = { lb.y / det, -la.y / det, 0.0f,
                      -lb.x / det, la.x / det, 0.0f,
                      0.0f, 0.0f, 0.0f };
        groups.push_back (group);
    }

    if (groups.empty())
    {
        error = "no usable speaker pairs (are the speakers spread around the listener?)";
        return false;
    }

    return true;
}

void Vbap::computeGains (Vec3 direction, float* gainsOut) const
{
    std::memset (gainsOut, 0, sizeof (float) * dirs.size());

    if (twoD)
    {
        direction.z = 0.0f;

        // Straight up or down has no horizontal direction: spread evenly
        if (length (direction) < 1.0e-6f)
        {
            const auto g = 1.0f / std::sqrt ((float) dirs.size());
            std::fill (gainsOut, gainsOut + dirs.size(), g);
            return;
        }
    }

    const auto p = normalised (direction);

    // Pick the group where the smallest gain is largest: the one containing p,
    // or the nearest one when p falls in a gap in the layout
    const Group* best = nullptr;
    float bestGains[3] = {};
    float bestMin = -1.0e9f;

    for (const auto& group : groups)
    {
        float g[3];
        applyInverse (group.inv, p, g);
        const auto minGain = group.size == 3 ? std::min ({ g[0], g[1], g[2] }) : std::min (g[0], g[1]);

        if (minGain > bestMin)
        {
            bestMin = minGain;
            best = &group;
            std::copy (g, g + 3, bestGains);
        }
    }

    if (best == nullptr)
        return;

    float power = 0.0f;
    for (int k = 0; k < best->size; ++k)
    {
        bestGains[k] = std::max (0.0f, bestGains[k]);
        power += bestGains[k] * bestGains[k];
    }

    if (power <= 0.0f)
        return;

    const auto norm = 1.0f / std::sqrt (power);
    for (int k = 0; k < best->size; ++k)
        gainsOut[best->idx[(size_t) k]] = bestGains[k] * norm;
}

} // namespace kuiper
