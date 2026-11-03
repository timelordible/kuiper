#pragma once

#include <array>
#include <string>
#include <vector>

namespace kuiper
{

// Cartesian direction: x = front, y = left, z = up (same as ATK and IEM).
struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Azimuth is counter-clockwise from the front (positive = left), elevation positive = up.
Vec3 directionFromAzEl (float azimuthRad, float elevationRad);

struct Speaker
{
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    bool imaginary = false;  // in the triangulation, but its gain is dropped
    int outputChannel = -1;  // 0-based device channel, -1 for imaginary speakers
};

// Vector Base Amplitude Panning over a speaker layout.
// 3D layouts are triangulated once in prepare(); layouts with every speaker at
// 0 deg elevation use pairwise 2D VBAP instead.
class Vbap
{
public:
    // Not real-time safe. Returns false and fills error if the layout is unusable.
    bool prepare (const std::vector<Speaker>& speakers, std::string& error);

    int numSpeakers() const { return (int) dirs.size(); }
    int numGroups() const { return (int) groups.size(); }
    bool is2D() const { return twoD; }

    // Real-time safe: no allocation, no locks.
    // Writes numSpeakers() gains with unit total power (sum of squares = 1).
    void computeGains (Vec3 direction, float* gainsOut) const;

private:
    struct Group
    {
        std::array<int, 3> idx {};
        std::array<float, 9> inv {};  // inverse of the speaker matrix, row-major
        int size = 3;                 // 3 for triangles, 2 for 2D pairs
    };

    bool triangulate3D (std::string& error);
    bool pair2D (std::string& error);

    std::vector<Vec3> dirs;
    std::vector<Group> groups;
    bool twoD = false;
};

} // namespace kuiper
