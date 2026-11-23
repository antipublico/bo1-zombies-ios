#include <platform/bo1_explore_motion.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
    using namespace bo1::explore;
    CameraState state{};
    Step(state, 1, 0, 0.05f, 2);
    assert(state.x == 0 && state.z < 0);
    const float forwardDistance = -state.z;
    CameraState right{};
    Step(right, 0, 1, 0.05f, 2);
    assert(right.x > 0 && right.z == 0);
    assert(std::fabs(forwardDistance - right.x) < 0.0001f);
    CameraState diagonal{};
    Step(diagonal, 1, 1, 0.05f, 2);
    assert(std::fabs(std::sqrt(diagonal.x * diagonal.x +
        diagonal.z * diagonal.z) - forwardDistance) < 0.0001f);

    CameraState turn{};
    Turn(turn, -392.699081f, 0); // yaw about +pi/2
    assert(std::fabs(turn.yaw - 1.5707963f) < 0.001f);
    Step(turn, 1, 0, 0.05f, 2);
    assert(turn.x < 0 && std::fabs(turn.z) < 0.001f);
    Turn(turn, 0, -99999.f);
    assert(turn.pitch <= 1.2f);
    Turn(turn, 0, 99999.f);
    assert(turn.pitch >= -1.2f);
    auto saved = turn;
    Turn(turn, std::numeric_limits<float>::quiet_NaN(), 3);
    assert(turn.yaw == saved.yaw && turn.pitch == saved.pitch);

    CameraState bounded{};
    for (int i = 0; i < 5000; ++i)
        Step(bounded, 0, 1, 0.5f, 2);
    assert(bounded.x <= 8.0f && bounded.x >= -8.0f);
    assert(bounded.y == 0);
    saved = bounded;
    Step(bounded, 1, 0, std::numeric_limits<float>::infinity(), 2);
    Step(bounded, 1, 0, 0.05f, 0);
    assert(bounded.x == saved.x && bounded.z == saved.z);
    std::puts("First-person diagnostic movement checks passed");
}
