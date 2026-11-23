#include <platform/bo1_explore_motion.h>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK FAILED: %s at %s:%d\\n", #condition, __FILE__, __LINE__); \
    std::exit(EXIT_FAILURE); \
} } while (false)

int main()
{
    using namespace bo1::explore;
    CameraState state{};
    Step(state, 1, 0, 0.05f, 2);
    CHECK(state.x == 0 && state.z < 0);
    const float forwardDistance = -state.z;
    CameraState right{};
    Step(right, 0, 1, 0.05f, 2);
    CHECK(right.x > 0 && right.z == 0);
    CHECK(std::fabs(forwardDistance - right.x) < 0.0001f);
    CameraState diagonal{};
    Step(diagonal, 1, 1, 0.05f, 2);
    CHECK(std::fabs(std::sqrt(diagonal.x * diagonal.x +
        diagonal.z * diagonal.z) - forwardDistance) < 0.0001f);

    CameraState turn{};
    Turn(turn, -392.699081f, 0); // yaw about +pi/2
    CHECK(std::fabs(turn.yaw - 1.5707963f) < 0.001f);
    Step(turn, 1, 0, 0.05f, 2);
    CHECK(turn.x < 0 && std::fabs(turn.z) < 0.001f);
    Turn(turn, 0, -99999.f);
    CHECK(turn.pitch <= 1.2f);
    Turn(turn, 0, 99999.f);
    CHECK(turn.pitch >= -1.2f);
    auto saved = turn;
    Turn(turn, std::numeric_limits<float>::quiet_NaN(), 3);
    CHECK(turn.yaw == saved.yaw && turn.pitch == saved.pitch);

    CameraState bounded{};
    for (int i = 0; i < 5000; ++i)
        Step(bounded, 0, 1, 0.5f, 2);
    CHECK(bounded.x <= 8.0f && bounded.x >= -8.0f);
    CHECK(bounded.y == 0);
    saved = bounded;
    Step(bounded, 1, 0, std::numeric_limits<float>::infinity(), 2);
    Step(bounded, 1, 0, 0.05f, 0);
    CHECK(bounded.x == saved.x && bounded.z == saved.z);
    std::puts("First-person diagnostic movement checks passed");
}
