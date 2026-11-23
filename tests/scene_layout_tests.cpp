#include <platform/bo1_scene_layout.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
    using namespace bo1::scene;
    std::vector<Placement> placements;
    assert(!BuildPlacements(0, 9, 2.f, placements));
    assert(!BuildPlacements(MaxSources + 1, 9, 2.f, placements));
    assert(!BuildPlacements(1, 0, 2.f, placements));
    assert(!BuildPlacements(1, MaxInstances + 1, 2.f, placements));
    assert(!BuildPlacements(1, 9, 0, placements));
    assert(!BuildPlacements(1, 9, std::numeric_limits<float>::infinity(), placements));
    assert(!BuildPlacements(1, 9, std::numeric_limits<float>::quiet_NaN(), placements));
    assert(BuildPlacements(1, 9, 2.f, placements));
    assert(placements.size() == 9);
    for (const auto& p : placements)
    {
        assert(p.sourceIndex == 0);
        assert(std::isfinite(p.x) && std::isfinite(p.z));
        assert(std::isfinite(p.yawRadians));
        assert(p.scale > 0.8f && p.scale < 1.2f);
        assert(std::fabs(p.x) < 2.5f && std::fabs(p.z) < 2.5f);
    }
    const auto copied = placements;
    assert(BuildPlacements(1, 9, 2.f, placements));
    for (std::size_t i = 0; i < 9; ++i)
    {
        assert(copied[i].x == placements[i].x);
        assert(copied[i].z == placements[i].z);
        assert(copied[i].yawRadians == placements[i].yawRadians);
    }
    assert(BuildPlacements(3, 9, 2.f, placements));
    bool present[3]{};
    for (const auto& p : placements)
    {
        assert(p.sourceIndex < 3);
        present[p.sourceIndex] = true;
    }
    assert(present[0] && present[1] && present[2]);
    assert(BuildPlacements(MaxSources, MaxInstances, 1.0f, placements));
    assert(placements.size() == MaxInstances);
    for (const auto& p : placements)
        assert(p.sourceIndex < MaxSources);
    std::puts("Deterministic multi-source SceneKit placement checks passed");
}
