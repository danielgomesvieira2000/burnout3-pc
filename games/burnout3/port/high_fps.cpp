/*
 * 120 pictures a second in a race (docs/HIGH_FPS_PLAN.md), FRAME RATE in
 * pc_settings.ini (frame_rate=120; 60 the game's own).
 *
 * The race steps 60 times a second - its physics, timers and AI take a
 * sixtieth each - and stays so. While a one-player race runs (its world
 * stepping: widescreen.cpp's RaceWorld; not in splitscreen, whose two views
 * already take what a machine has), the vertical blanks come twice as often
 * (SetFastForward(2)): the game draws a picture at each, and its world
 * (0x0019A550) is stepped at every second one only.
 *
 * The pictures between: what moves is placed between steps. After each
 * step every transform of the race's objects (RenderWare's RwMatrix:
 * right, up, at, pos, the first three orthonormal - the cars, their wheels,
 * the traffic, found by shape: engine/tools/find_transforms.py) is taken
 * down, and for the picture drawn then each one that moved is placed half
 * way between where the last step and this one left it; before the next
 * picture (and the next step) it is put back. So the pictures show the race
 * at steps N - 1/2, N, N + 1/2, N + 1: evenly, half a step late. The camera
 * is made in the draw, from the cars: it follows them by itself. A matrix
 * that jumped further than a car moves in a step (a cut, a respawn) is not
 * blended.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

unsigned long long RaceWorldCalls();    // widescreen.cpp

namespace {

constexpr u32 kPlayers = 0x0064D3A0u;   // the race's players (2 or more: splitscreen, party modes)
// Where the race's objects and their transforms live (find_transforms.py:
// every moving one between 0x00517000 and 0x00660000).
constexpr u32 kObjectsBegin = 0x00500000u, kObjectsEnd = 0x00700000u;
constexpr float kCut = 50.0f;           // further a step than this: a cut, not motion

bool g_active = false;
unsigned long long g_pictures = 0;      // since 120 began: the world steps on the even ones

struct Matrix {
    float m[16];
};
std::unordered_map<u32, Matrix> g_last, g_now;      // the transforms after the last two steps
std::vector<std::pair<u32, Matrix>> g_placed;       // what the in-between placing covered

/* FRAME RATE: PS2HOST_FRAME_RATE, else the setting. */
bool Wanted() {
    static const char* env = std::getenv("PS2HOST_FRAME_RATE");
    if (env) return std::atoi(env) == 120;
    return ps2host::Setting("frame_rate", "60") == "120";
}

/* A race running: its world asked to step within the last few pictures (as
   controls.cpp's Racing; still in the pause menu, the frontend, a loading
   screen). */
bool Racing() {
    static unsigned long long seen = 0;
    static int still = 99;
    const unsigned long long calls = RaceWorldCalls();
    still = calls != seen ? 0 : still + 1;
    seen = calls;
    return still <= 8;
}

float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* An RwMatrix: its right, up and at rows of length 1 and square to each
   other, its position finite. */
bool Shaped(const float* m) {
    constexpr float kTolerance = 0.01f;
    for (int row = 0; row < 3; ++row)
        if (!(std::fabs(Dot3(m + 4 * row, m + 4 * row) - 1.0f) < kTolerance)) return false;
    if (!(std::fabs(Dot3(m, m + 4)) < kTolerance && std::fabs(Dot3(m, m + 8)) < kTolerance &&
          std::fabs(Dot3(m + 4, m + 8)) < kTolerance))
        return false;
    for (int k = 12; k < 15; ++k)
        if (!(std::fabs(m[k]) < 1e6f)) return false;
    return true;
}

/* The transforms as the step left them. */
void TakeDown() {
    g_last.swap(g_now);
    g_now.clear();
    const u8* base = generated::RamPointer(kObjectsBegin, kObjectsEnd - kObjectsBegin);
    if (!base) return;
    for (u32 at = 0; at + 64 <= kObjectsEnd - kObjectsBegin; at += 16) {
        Matrix m;
        std::memcpy(m.m, base + at, sizeof m.m);
        if (!Shaped(m.m)) continue;
        g_now.emplace(kObjectsBegin + at, m);
        at += 48;                       // (its rows are not another's)
    }
}

void Normalise(float* v) {
    const float length = std::sqrt(Dot3(v, v));
    if (length > 0.0f)
        for (int k = 0; k < 3; ++k) v[k] /= length;
}

/* Half way between two transforms: the position the mean, the rotation the
   rows' means made orthonormal again (at, then up square to it, then right
   from the two, its handedness the original's). */
Matrix Between(const Matrix& a, const Matrix& b) {
    Matrix out = b;
    float right[3], up[3], at[3];
    for (int k = 0; k < 3; ++k) {
        right[k] = a.m[k] + b.m[k];
        up[k] = a.m[4 + k] + b.m[4 + k];
        at[k] = a.m[8 + k] + b.m[8 + k];
        out.m[12 + k] = 0.5f * (a.m[12 + k] + b.m[12 + k]);
    }
    Normalise(at);
    const float d = Dot3(up, at);
    for (int k = 0; k < 3; ++k) up[k] -= d * at[k];
    Normalise(up);
    float cross[3] = {up[1] * at[2] - up[2] * at[1], up[2] * at[0] - up[0] * at[2], up[0] * at[1] - up[1] * at[0]};
    if (Dot3(cross, right) < 0.0f)
        for (float& x : cross) x = -x;
    for (int k = 0; k < 3; ++k) {
        out.m[k] = cross[k];
        out.m[4 + k] = up[k];
        out.m[8 + k] = at[k];
    }
    return out;
}

/* For the picture drawn after a step: each transform that moved, half way
   back towards the step before. */
void PlaceBetween() {
    g_placed.clear();
    for (const auto& [address, now] : g_now) {
        const auto last = g_last.find(address);
        if (last == g_last.end()) continue;
        float d[3];
        for (int k = 0; k < 3; ++k) d[k] = now.m[12 + k] - last->second.m[12 + k];
        const float moved = std::sqrt(Dot3(d, d));
        bool turned = false;
        for (int k = 0; k < 12 && !turned; ++k) turned = now.m[k] != last->second.m[k];
        if ((moved == 0.0f && !turned) || moved > kCut) continue;
        const Matrix between = Between(last->second, now);
        u8* p = generated::RamPointer(address, 64);
        if (!p) continue;
        g_placed.emplace_back(address, now);
        std::memcpy(p, between.m, 12 * 4);          // the rows (their fourth words as they were)
        std::memcpy(p + 48, between.m + 12, 3 * 4);
    }
}

/* What the placing covered, back as the step left it. */
void PutBack() {
    for (const auto& [address, m] : g_placed)
        if (u8* p = generated::RamPointer(address, 64)) std::memcpy(p, m.m, sizeof m.m);
    g_placed.clear();
}

}  // namespace

/* Each picture (mods.cpp's Frame): 120 on or off as the race and the
   setting say. */
void HighFpsFrame() {
    const bool want = Racing() && Wanted() && generated::ReadS32(kPlayers) < 2;
    if (want == g_active) return;
    std::fprintf(stderr, "[high fps] %s\n", want ? "120 pictures a second" : "60 pictures a second");
    g_active = want;
    g_pictures = 0;
    PutBack();
    g_last.clear();
    g_now.clear();
    ps2host::SetFastForward(want ? 2 : 1);
}

/* The race's world about to step (RaceWorld), each picture: what the last
   picture placed put back; false where it does not step (every second
   picture while at 120). */
bool HighFpsStepWorld() {
    PutBack();
    if (!g_active) return true;
    return (g_pictures++ & 1) == 0;
}

/* The world has stepped (RaceWorld): at 120 its transforms taken down, and
   placed between this step and the last for the picture drawn now. */
void HighFpsWorldStepped() {
    if (!g_active) return;
    static const bool blend = std::getenv("PS2HOST_HIGH_FPS_NO_BLEND") == nullptr;   // to compare
    TakeDown();
    if (blend) PlaceBetween();
}
