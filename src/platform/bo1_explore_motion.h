#pragma once
#include <algorithm>
#include <cmath>

// Tiny portable movement state for the staged 3D *diagnostic*. This does not
// reproduce BO1 movement, collision, gravity or gameplay. No UIKit dependency.
namespace bo1::explore
{
struct CameraState
{
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
};

inline void Turn(CameraState& state, float dx, float dy) noexcept
{
    if (!std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(state.yaw) || !std::isfinite(state.pitch))
        return;
    const float clampedX = std::clamp(dx, -2000.f, 2000.f);
    const float clampedY = std::clamp(dy, -2000.f, 2000.f);
    state.yaw = std::remainder(state.yaw - clampedX * 0.004f,
        6.283185307179586f);
    state.pitch = std::clamp(state.pitch - clampedY * 0.003f, -1.2f, 1.2f);
}

inline void Step(CameraState& state, float forward, float right,
    float deltaSeconds, float span) noexcept
{
    if (!std::isfinite(forward) || !std::isfinite(right) ||
        !std::isfinite(deltaSeconds) || !std::isfinite(span) ||
        !std::isfinite(state.x) || !std::isfinite(state.z) ||
        !std::isfinite(state.yaw) || span <= 0)
        return;
    const float magnitude = std::sqrt(forward * forward + right * right);
    if (magnitude < 0.001f || !std::isfinite(magnitude) ||
        deltaSeconds <= 0)
        return;
    const float dt = std::clamp(deltaSeconds, 0.f, 0.05f);
    const float scaled = std::min(9000.f, std::max(0.3f, span));
    const float distance = dt * std::max(0.5f, scaled * 1.6f) / magnitude;
    state.x += (-std::sin(state.yaw) * forward
        + std::cos(state.yaw) * right) * distance;
    state.z += (-std::cos(state.yaw) * forward
        - std::sin(state.yaw) * right) * distance;
    const float bound = scaled * 4.0f;
    state.x = std::clamp(state.x, -bound, bound);
    state.z = std::clamp(state.z, -bound, bound);
}
}
