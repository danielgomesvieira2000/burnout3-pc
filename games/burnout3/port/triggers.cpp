/*
 * A DualSense's adaptive triggers while racing (TRIGGER EFFECTS in PC
 * SETTINGS > CONTROLS): the accelerator and the brake pushing back as the
 * car does, not a constant spring.
 *
 * What the car is doing comes from the race's own state, read once a pad
 * read, its rate the race clock's (0x0064E47C, which slows with the game's
 * slow motion - so the speed is the car's, not the picture's):
 *
 *   - player 1's car's matrix at 0x0064D3B0 (RenderWare's: right, up, at,
 *     then the position at +0x30 - the same place as the race block's car,
 *     0x0064E710, which is how it is known to be the car's); found in a
 *     scripted drive by the position. From two positions and the clock, the
 *     velocity; in the car's own axes, the speed forward (negative backing
 *     up) and sideways - their angle is the slip: 0 to 1 degree driving or
 *     braking straight, 6-8 sliding at 28 mph into a turn, 180 reversing.
 *   - the speed forward's change: the engine's pull (6 m/s^2 at 60 mph full
 *     throttle, 2 at 105), the brakes' (to about 22 m/s^2 from 80 mph) and
 *     a hit's step (112 to 89 mph within a step in the first race's shunt).
 *
 * The trigger's travel is the race's band of pressure (controls.cpp): the
 * first fifth coasts, from there to four fifths the throttle (or brake)
 * rises, beyond it is all of it. A DualSense's resistance is set by tenths
 * of the travel (zones, 0-8 each), so the effect has a shape:
 *
 *   ACCELERATOR - free through the coasting play; a light spring through
 *   the throttle that firms with the engine's pull (heavier while the car
 *   is really accelerating, lighter as it nears its top speed); a firmer
 *   step at full throttle, the stop where the engine has nothing more - not
 *   a wall, it is pressed through. When the rear lets go - sliding with the
 *   throttle on, or spinning up from a standstill - the spring goes light
 *   and loose, fluttering: the traction gone.
 *
 *   BRAKE - free until the pads touch, then a slope that steepens with the
 *   speed (there is more to stop), firmest in the band's top where the
 *   brakes do their work. As the car nears the grip's limit - the brakes'
 *   deceleration near the most the car has, or the car's slip and cornering
 *   load growing while braking - the brake starts to pulse, as ABS does,
 *   gently at first; past it (sliding) the pulse slows to a judder and the
 *   top softens: the wheels have let go. At a crawl, backing up, the brake
 *   is the reverse: light and even.
 *
 *   A hit buzzes both triggers for a moment, by how hard; a wrecked car (on
 *   its side or spinning) leaves them free - it is not the player's to
 *   drive. Changes rise quickly and fall back more gently, so letting off
 *   is smooth. LIGHT is the same, at about three fifths.
 *
 * B3_TRIGGERS_LOG=1 prints the car's state and both triggers' zones a few
 * times a second.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "triggers.h"

namespace triggers {
namespace {

constexpr u32 kCarMatrix = 0x0064D3B0u;     // right, up, at (16 bytes each), position
constexpr u32 kRaceCar = 0x0064E710u;       // the race block's copy of its position
constexpr u32 kRaceClock = 0x0064E47Cu;     // seconds, the game's (slowed in slow motion)
constexpr float kPi = 3.14159265f;

float F32(u32 address) {
    const u32 bits = generated::ReadU32(address);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

struct Vec { float x = 0, y = 0, z = 0; };
Vec ReadVec(u32 at) { return {F32(at), F32(at + 4u), F32(at + 8u)}; }
float Dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Sub(const Vec& a, const Vec& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
bool Finite(const Vec& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float Clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* The car as the triggers see it, from the race's state (above). */
struct Car {
    bool valid = false;         // the matrix is the car's and the clock runs
    bool have_last = false;
    Vec last_pos;
    float last_clock = 0.0f, last_yaw = 0.0f;
    float forward = 0.0f;       // m/s along its nose (negative backing up)
    float sideways = 0.0f;      // m/s across it
    float accel = 0.0f;         // m/s^2 forward, smoothed
    float yaw_rate = 0.0f;      // rad/s, smoothed
    float slip = 0.0f;          // rad, the velocity's angle off the nose (forward or backward)
    float up_y = 1.0f;          // the roof's up: 1 on its wheels
    float rate = 1.0f;          // the race clock's seconds a real second (slow motion below 1)
    float hit = 0.0f;           // a hit's strength this step, 0-1
    float settling = 0.0f;      // race seconds since it was placed: no hits read until 0.5
};
Car g_car;

using Clock = std::chrono::steady_clock;
Clock::time_point g_last_real = Clock::now();

/* Once a pad read: the car's state, when the race clock moved on. */
void ReadCar(float real_dt) {
    Car& c = g_car;
    c.hit = 0.0f;
    const Vec right = ReadVec(kCarMatrix), up = ReadVec(kCarMatrix + 0x10u), at = ReadVec(kCarMatrix + 0x20u);
    const Vec pos = ReadVec(kCarMatrix + 0x30u), race = ReadVec(kRaceCar);
    const float clock = F32(kRaceClock);
    const Vec off = Sub(pos, race);
    c.valid = Finite(right) && Finite(up) && Finite(at) && Finite(pos) && std::isfinite(clock) &&
              Dot(off, off) < 0.25f && std::fabs(Dot(at, at) - 1.0f) < 0.05f;
    if (!c.valid) {
        c.have_last = false;
        return;
    }
    c.up_y = up.y;
    const float dt = clock - c.last_clock;
    const float yaw = std::atan2(at.x, at.z);
    if (!c.have_last || dt < 0.0f || dt > 0.25f) {         // a new race, a restart
        c = Car{true, true, pos, clock, yaw};
        c.up_y = up.y;
        return;
    }
    if (dt < 1e-4f) return;                                 // paused, or a picture between steps
    const Vec moved = Sub(pos, c.last_pos);
    // Placed elsewhere (back on the road after a wreck): faster than any car
    // goes, 120 m/s (270 mph).
    if (Dot(moved, moved) > (120.0f * dt) * (120.0f * dt)) {
        c = Car{true, true, pos, clock, yaw};
        c.up_y = up.y;
        return;
    }
    const Vec v = {moved.x / dt, moved.y / dt, moved.z / dt};
    const float forward = Dot(v, at), sideways = Dot(v, right);
    const float speed = std::sqrt(forward * forward + sideways * sideways);
    // A hit: the speed knocked off within a step, far past any brake's.
    const float before = std::sqrt(c.forward * c.forward + c.sideways * c.sideways);
    c.settling += dt;
    if (before - speed > 3.0f && c.settling > 0.5f) c.hit = Clamp01((before - speed) / 15.0f);
    const float a = (forward - c.forward) / dt;
    float turn = yaw - c.last_yaw;
    while (turn > kPi) turn -= 2.0f * kPi;
    while (turn < -kPi) turn += 2.0f * kPi;
    const float k = 1.0f - std::exp(-dt / 0.08f);           // ~80 ms smoothing
    if (c.hit == 0.0f) c.accel += (a - c.accel) * k;        // (a hit's step is not the engine's or the brakes')
    c.yaw_rate += (turn / dt - c.yaw_rate) * k;
    c.forward = forward;
    c.sideways = sideways;
    c.slip = speed > 2.0f ? std::atan2(std::fabs(sideways), std::fabs(forward)) : 0.0f;
    if (real_dt > 1e-4f) c.rate += (Clamp01(dt / real_dt) - c.rate) * 0.2f;
    c.last_pos = pos;
    c.last_clock = clock;
    c.last_yaw = yaw;
}

/* A trigger's zones as they are felt: each eases towards its target - up in
   ~40 ms, back down in ~120 ms - before any pulse is laid on. */
struct Shape {
    float level[10] = {};
    void Ease(const float* target, float dt) {
        const float up = 1.0f - std::exp(-dt / 0.04f), down = 1.0f - std::exp(-dt / 0.12f);
        for (int i = 0; i < 10; ++i) level[i] += (target[i] - level[i]) * (target[i] > level[i] ? up : down);
    }
    void Clear() { std::fill(std::begin(level), std::end(level), 0.0f); }
};
Shape g_accel, g_brake;
float g_hit = 0.0f;             // the hit's buzz left, 0-1
float g_phase = 0.0f;           // the pulses' clock (s)

// The band of the travel the race reads (controls.cpp's): coasting below
// zone 2, all of it from zone 8.
constexpr int kBandFirst = 2, kBandLast = 7;

void Accelerator(const Car& c, float* z) {
    std::fill(z, z + 10, 0.0f);
    const float speed = std::fabs(c.forward);
    const float load = Clamp01(c.accel / 7.0f);             // the engine's pull
    const float top = Clamp01((speed - 30.0f) / 20.0f);     // nearing its top speed (about 110 mph)
    for (int i = kBandFirst; i <= kBandLast; ++i)
        z[i] = 1.0f + float(i - kBandFirst) / float(kBandLast - kBandFirst) * (1.0f + 1.5f * load);
    const float stop = z[kBandLast] + 1.5f + 1.0f * load - 0.75f * top * (1.0f - load);
    z[8] = z[9] = stop;
}

/* The rear letting go, 0-1: sliding with the throttle on, or spinning up
   from a standstill with it floored. */
float TractionLost(const Car& c, float throttle) {
    const float speed = std::fabs(c.forward);
    const float slide = speed > 5.0f ? Clamp01((c.slip - 4.0f * kPi / 180.0f) / (10.0f * kPi / 180.0f)) : 0.0f;
    // (moving off and pulling hard: not the countdown's revving, or pinned to a wall)
    const float launch = throttle > 0.7f && c.forward > 0.5f && speed < 10.0f && c.accel > 2.0f
                             ? 0.6f * (1.0f - speed / 10.0f) : 0.0f;
    return throttle > 0.25f ? std::max(slide, launch) : 0.0f;
}

void Brake(const Car& c, float* z) {
    std::fill(z, z + 10, 0.0f);
    if (c.forward < 1.5f) {                                 // stopped or backing up: the reverse
        for (int i = 1; i < 10; ++i) z[i] = i >= 8 ? 2.0f : 1.0f;
        return;
    }
    const float speed = Clamp01(c.forward / 45.0f);
    z[1] = speed > 0.2f ? 1.0f : 0.0f;                      // the pads touching
    for (int i = kBandFirst; i < 10; ++i) {
        const float along = Clamp01(float(i - kBandFirst) / float(9 - kBandFirst));
        z[i] = 1.5f + along * (1.5f + 4.0f * speed) + (i >= 6 ? 0.5f * speed : 0.0f);
    }
}

/* How near the grip's limit braking is: 1 at it. The brakes' deceleration
   against the most the car has, the cornering load, the slip. */
float GripUsed(const Car& c) {
    const float decel = std::max(0.0f, -c.accel) / 22.0f;
    const float lateral = std::fabs(c.forward * c.yaw_rate) / 16.0f;      // (about 1.6 g)
    const float slip = (c.slip - 2.0f * kPi / 180.0f) / (10.0f * kPi / 180.0f);
    return std::max(decel, std::max(lateral, slip));
}

void ToZones(const float* level, float scale, ps2host::HostTriggerEffect* e) {
    e->kind = ps2host::HostTriggerEffect::kZones;
    bool any = false;
    for (int i = 0; i < 10; ++i) {
        const float v = level[i] * scale;
        e->zones[i] = u8(std::clamp(int(std::lround(v)), 0, 8));
        any |= e->zones[i] != 0;
    }
    if (!any) *e = ps2host::HostTriggerEffect{};
}

const bool kLog = std::getenv("B3_TRIGGERS_LOG") != nullptr;

}  // namespace

void Tick(bool racing, float throttle, float brake, ps2host::HostTriggerEffect* accel_out,
          ps2host::HostTriggerEffect* brake_out) {
    *accel_out = *brake_out = ps2host::HostTriggerEffect{};
    const Clock::time_point now = Clock::now();
    const float real_dt = std::min(0.1f, std::chrono::duration<float>(now - g_last_real).count());
    g_last_real = now;
    const std::string setting = ps2host::Setting("trigger_effects", "on");
    const float scale = setting == "light" ? 0.6f : setting == "off" ? 0.0f : 1.0f;
    if (!racing || scale == 0.0f) {
        g_car.have_last = false;
        g_accel.Clear();
        g_brake.Clear();
        g_hit = 0.0f;
        return;
    }
    ReadCar(real_dt);
    const Car& c = g_car;
    g_phase += real_dt;
    if (!c.valid) {
        // Not the race's car (another mode's): the shape alone, no state.
        Car still;
        float za[10], zb[10];
        Accelerator(still, za);
        still.forward = 20.0f;
        Brake(still, zb);
        g_accel.Ease(za, real_dt);
        g_brake.Ease(zb, real_dt);
        ToZones(g_accel.level, scale, accel_out);
        ToZones(g_brake.level, scale, brake_out);
        return;
    }
    // A wreck: on its side or roof, or spinning - free triggers.
    const bool wrecked = c.up_y < 0.4f || (c.slip > 1.2f && std::fabs(c.sideways) > 8.0f);
    g_hit = std::max(g_hit * std::exp(-real_dt / 0.07f), c.hit);
    if (kLog && c.hit > 0.0f) std::fprintf(stderr, "[triggers] hit %.2f at %.1f mph\n", c.hit, c.forward * 2.237f);
    if (g_hit > 0.15f) {
        // The hit's buzz, both triggers, through the throttle's and the brake's band.
        ps2host::HostTriggerEffect buzz;
        buzz.kind = ps2host::HostTriggerEffect::kZoneVibration;
        buzz.frequency = 24;
        for (int i = 1; i < 10; ++i) buzz.zones[i] = u8(std::clamp(int(std::lround((2.0f + 6.0f * g_hit) * scale)), 1, 8));
        *accel_out = *brake_out = buzz;
        g_accel.Clear();
        g_brake.Clear();
        return;
    }
    if (wrecked) {
        g_accel.Clear();
        g_brake.Clear();
        return;
    }
    // In slow motion (a takedown's camera) the car is still driven: lighter.
    const float slow = c.rate < 0.7f ? 0.7f : 1.0f;

    float za[10];
    Accelerator(c, za);
    g_accel.Ease(za, real_dt);
    float accel[10];
    std::copy(std::begin(g_accel.level), std::end(g_accel.level), accel);
    const float lost = TractionLost(c, throttle);
    if (lost > 0.0f) {
        const bool flutter = std::fmod(g_phase * 10.0f, 1.0f) < 0.5f;      // 10 Hz
        for (int i = kBandFirst; i < 10; ++i)
            accel[i] = std::max(0.0f, accel[i] * (1.0f - 0.5f * lost) - (flutter ? 2.0f * lost : 0.0f));
    }

    float zb[10];
    Brake(c, zb);
    g_brake.Ease(zb, real_dt);
    float brake_zones[10];
    std::copy(std::begin(g_brake.level), std::end(g_brake.level), brake_zones);
    const float used = brake > 0.25f && c.forward > 5.0f ? GripUsed(c) : 0.0f;
    const float nearing = Clamp01((used - 0.7f) / 0.3f), past = Clamp01((used - 1.0f) / 0.5f);
    if (nearing > 0.0f) {
        // ABS: from where the brake is held on, a pulse - 14 Hz near the
        // limit, slowing to 8 Hz past it, the top softening as the wheels go.
        const float hz = 14.0f - 6.0f * past;
        const bool off = std::fmod(g_phase * hz, 1.0f) < 0.5f;
        const int from = std::max(kBandFirst, int(brake * 10.0f) - 1);
        for (int i = from; i < 10; ++i) {
            if (off) brake_zones[i] -= 1.0f + 2.0f * nearing + 1.0f * past;
            if (i >= 8) brake_zones[i] -= 1.5f * past;
            brake_zones[i] = std::max(1.0f, brake_zones[i]);
        }
    }
    ToZones(accel, scale * slow, accel_out);
    ToZones(brake_zones, scale * slow, brake_out);

    if (kLog) {
        static float printed = 0.0f;
        printed += real_dt;
        if (printed >= 0.25f) {
            printed = 0.0f;
            std::fprintf(stderr, "[triggers] %5.1f mph fwd %6.2f m/s accel %6.2f slip %5.1f yaw %5.2f rate %.2f "
                                 "lost %.2f grip %.2f | R", c.forward * 2.237f, c.forward, c.accel,
                         c.slip * 180.0f / kPi, c.yaw_rate, c.rate, lost, used);
            for (int i = 0; i < 10; ++i) std::fprintf(stderr, "%d", accel_out->zones[i]);
            std::fprintf(stderr, " L");
            for (int i = 0; i < 10; ++i) std::fprintf(stderr, "%d", brake_out->zones[i]);
            std::fputc('\n', stderr);
        }
    }
}

}  // namespace triggers
