/*
 * MODS > ORBIT CAMERA (off by default): the controller's right stick turns
 * the race camera around the player's car - left and right all the way
 * round to the back, up and down a little - and lets it swing back behind
 * when let go, as Burnout Revenge's does. The right stick is the camera's
 * then, not the throttle (the game's own right-stick accelerate and brake).
 * The idea is from Remco's (SuperType1's) PCSX2 orbit camera; this is the
 * port's own, on the game's camera values.
 *
 * The camera's values (high_fps.cpp): player 1's at 0x00517030, player 2's
 * (splitscreen) 0xBB0 on - its direction a quaternion at +0x20 (x, y, z, w;
 * +z the way it looks, the world's y up) and where it is at +0x30. The race's
 * ticks set them; the draw makes the view from them. Here they are turned
 * around the car just before the picture is drawn (the display's frame step,
 * after a high frame rate's placing) and put back as the next frame starts
 * (before that placing's own putting back) - the game's camera never sees
 * the turn, and its chase goes on as it was.
 *
 * PS2HOST_ORBIT_TEST=yaw,pitch (degrees): the camera held so turned (a
 * scripted run's test: its presses never reach the controller's filter;
 * with --setting mod.orbit_camera=on, the camera being off by default).
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

bool OrbitCameraOn() { return ps2host::SettingOn("mod.orbit_camera", false); }

namespace {

constexpr u32 kCamera[2] = {0x00517030u, 0x00517030u + 0xBB0u};
constexpr u32 kCar[2] = {0x0064E710u, 0x0064E710u + 0x27D0u};    // the race block's place of each car
constexpr float kPi = 3.14159265f;
constexpr float kMostYaw = 170.0f * kPi / 180.0f;     // full left or right: round to the back
constexpr float kMostPitch = 25.0f * kPi / 180.0f;

struct Orbit {
    float stick[2] = {};        // the right stick, -1..1 (x right, y up), dead zone taken off
    bool stick_seen = false;
    float yaw = 0.0f, pitch = 0.0f;     // as turned now (eased towards the stick's)
    bool placed = false;        // turned for this picture: its values to put back
    float was[7] = {}, put[7] = {};     // the game's (quaternion, place) and what was written
    float pivot_depth = 5.0f;   // how far ahead of the camera the car is, eased
};
Orbit g_orbit[2];

float F(u32 a) {
    const u32 bits = generated::ReadU32(a);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
void SetF(u32 a, float f) {
    u32 bits;
    std::memcpy(&bits, &f, 4);
    generated::Write32(a, bits);
}

float Axis(u8 v) {
    const float x = (float(v) - 127.5f) / 127.5f;
    const float dead = 0.18f;
    if (std::fabs(x) < dead) return 0.0f;
    return std::clamp((std::fabs(x) - dead) / (1.0f - dead), 0.0f, 1.0f) * (x < 0 ? -1.0f : 1.0f);
}

struct Quat {
    float x, y, z, w;
};
Quat Mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Quat AxisAngle(float ax, float ay, float az, float angle) {
    const float s = std::sin(angle * 0.5f);
    return {ax * s, ay * s, az * s, std::cos(angle * 0.5f)};
}
void Rotate(const Quat& q, const float* v, float* out) {
    // v + 2w(u x v) + 2u x (u x v)
    const float ux = q.x, uy = q.y, uz = q.z;
    const float t[3] = {2.0f * (uy * v[2] - uz * v[1]), 2.0f * (uz * v[0] - ux * v[2]), 2.0f * (ux * v[1] - uy * v[0])};
    out[0] = v[0] + q.w * t[0] + (uy * t[2] - uz * t[1]);
    out[1] = v[1] + q.w * t[1] + (uz * t[0] - ux * t[2]);
    out[2] = v[2] + q.w * t[2] + (ux * t[1] - uy * t[0]);
}

bool TestAngles(float* yaw, float* pitch) {
    static const char* env = std::getenv("PS2HOST_ORBIT_TEST");
    if (!env) return false;
    float y = 0, p = 0;
    if (std::sscanf(env, "%f,%f", &y, &p) < 1) return false;
    *yaw = y * kPi / 180.0f;
    *pitch = p * kPi / 180.0f;
    return true;
}

}  // namespace

/* The controller's filter (controls.cpp), each pad read: the right stick
   kept for the camera and, racing with the camera on, centred for the game. */
void OrbitStick(ps2host::HostPadState* pad, bool racing) {
    Orbit& o = g_orbit[pad->port & 1];
    o.stick[0] = Axis(pad->sticks[0]);
    o.stick[1] = -Axis(pad->sticks[1]);
    o.stick_seen = true;
    if (racing && OrbitCameraOn()) {
        pad->sticks[0] = 0x80;
        pad->sticks[1] = 0x80;
    }
}

/* Each picture before its draw (mods.cpp's frame step, after the high frame
   rate's placing): each view's camera turned around its car. */
void OrbitFrame(bool racing) {
    const bool on = racing && OrbitCameraOn();
    for (int view = 0; view < 2; ++view) {
        Orbit& o = g_orbit[view];
        if (o.placed) continue;                 // (the frame step runs a few times a picture)
        // Eased towards the stick's angles: a quick turn, a swing back.
        float want_yaw = on ? -o.stick[0] * kMostYaw : 0.0f, want_pitch = on ? o.stick[1] * kMostPitch : 0.0f;
        const bool test = on && TestAngles(&want_yaw, &want_pitch);
        const float dt = float(ps2host::BlankPeriodNs()) * 1e-9f;
        const float k = test ? 1.0f : 1.0f - std::exp(-dt / 0.09f);
        o.yaw += (want_yaw - o.yaw) * k;
        o.pitch += (want_pitch - o.pitch) * k;
        if (!on || (std::fabs(o.yaw) < 1e-3f && std::fabs(o.pitch) < 1e-3f)) {
            if (!on) o.yaw = o.pitch = 0.0f;
            continue;
        }
        const u32 cam = kCamera[view];
        for (int i = 0; i < 4; ++i) o.was[i] = F(cam + 0x20u + 4u * i);
        for (int i = 0; i < 3; ++i) o.was[4 + i] = F(cam + 0x30u + 4u * i);
        const Quat q{o.was[0], o.was[1], o.was[2], o.was[3]};
        const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (!(len > 0.9f && len < 1.1f)) continue;      // (no camera there: a menu's)
        const float* pos = &o.was[4];
        // The pivot: on the camera's own line of sight, as far as the car is
        // along it (eased: the car's place is the tick's, the camera's blended).
        const float zaxis[3] = {0, 0, 1};
        float fwd[3];
        Rotate(q, zaxis, fwd);
        const u32 car = kCar[view];
        const float to[3] = {F(car) - pos[0], F(car + 4u) - pos[1], F(car + 8u) - pos[2]};
        const float depth = to[0] * fwd[0] + to[1] * fwd[1] + to[2] * fwd[2];
        if (depth > 1.0f && depth < 30.0f) o.pivot_depth += (depth - o.pivot_depth) * 0.2f;
        const float pivot[3] = {pos[0] + fwd[0] * o.pivot_depth, pos[1] + fwd[1] * o.pivot_depth,
                                pos[2] + fwd[2] * o.pivot_depth};
        // Yaw about the world's up, then pitch about the camera's right as turned.
        const Quat qy = AxisAngle(0, 1, 0, o.yaw);
        Quat q1 = Mul(qy, q);
        const float xaxis[3] = {1, 0, 0};
        float right[3];
        Rotate(q1, xaxis, right);
        const Quat qp = AxisAngle(right[0], right[1], right[2], o.pitch);
        const Quat q2 = Mul(qp, q1);
        const float rel[3] = {pos[0] - pivot[0], pos[1] - pivot[1], pos[2] - pivot[2]};
        float r1[3], r2[3];
        Rotate(qy, rel, r1);
        Rotate(qp, r1, r2);
        o.put[0] = q2.x;
        o.put[1] = q2.y;
        o.put[2] = q2.z;
        o.put[3] = q2.w;
        for (int i = 0; i < 3; ++i) o.put[4 + i] = pivot[i] + r2[i];
        for (int i = 0; i < 4; ++i) SetF(cam + 0x20u + 4u * i, o.put[i]);
        for (int i = 0; i < 3; ++i) SetF(cam + 0x30u + 4u * i, o.put[4 + i]);
        o.placed = true;
    }
}

/* As the next frame starts (high_fps.cpp, before the high frame rate's
   putting back): the game's camera values as they were, where still as
   turned. */
void OrbitRestore() {
    for (int view = 0; view < 2; ++view) {
        Orbit& o = g_orbit[view];
        if (!o.placed) continue;
        o.placed = false;
        const u32 cam = kCamera[view];
        bool same = true;
        for (int i = 0; i < 4 && same; ++i) same = F(cam + 0x20u + 4u * i) == o.put[i];
        for (int i = 0; i < 3 && same; ++i) same = F(cam + 0x30u + 4u * i) == o.put[4 + i];
        if (!same) continue;
        for (int i = 0; i < 4; ++i) SetF(cam + 0x20u + 4u * i, o.was[i]);
        for (int i = 0; i < 3; ++i) SetF(cam + 0x30u + 4u * i, o.was[4 + i]);
    }
}
