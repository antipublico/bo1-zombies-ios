#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

// Repeat and arrange CPU-owned meshes from user-selected FF files. Placements
// are deterministic, bounded and fully synthetic; no real map coordinates,
// terrain, vegetation spawning rules or original BO1 material assignments.
namespace bo1::scene
{
inline constexpr std::size_t MaxSources = 6;
inline constexpr std::size_t MaxInstances = 16;

struct Placement
{
    std::size_t sourceIndex = 0;
    float x = 0, z = 0;
    float yawRadians = 0;
    float scale = 1;
};

inline bool BuildPlacements(std::size_t sourceCount, std::size_t instanceCount,
    float spacing, std::vector<Placement>& output) noexcept
{
    output.clear();
    if (sourceCount == 0 || sourceCount > MaxSources ||
        instanceCount == 0 || instanceCount > MaxInstances ||
        !std::isfinite(spacing) || spacing < 0.01f || spacing > 10000.f)
        return false;
    const std::size_t side = instanceCount <= 4 ? 2 : instanceCount <= 9 ? 3 : 4;
    const float center = float(side - 1) * 0.5f;
    output.reserve(instanceCount);
    for (std::size_t i = 0; i < instanceCount; ++i)
    {
        const int jitterX = int((i * 11 + 2) % 7) - 3;
        const int jitterZ = int((i * 17 + 1) % 9) - 4;
        Placement p{};
        p.sourceIndex = (i + i / side) % sourceCount;
        p.x = (float(i % side) - center + float(jitterX) * 0.022f) * spacing;
        p.z = (float(i / side) - center + float(jitterZ) * 0.017f) * spacing;
        p.yawRadians = float((i * 73 + 19) % 360) * 0.017453292519943295f;
        p.scale = 0.86f + float((i * 7 + 3) % 11) * 0.026f;
        output.push_back(p);
    }
    return true;
}
}
