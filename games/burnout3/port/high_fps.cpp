/*
 * 120 pictures a second in a race, or the screen's own rate (docs/
 * HIGH_FPS_PLAN.md), FRAME RATE in pc_settings.ini (frame_rate=120 or
 * display; 60 the game's own).
 *
 * The race ticks 60 times a second - its physics, clock, crashes and AI
 * take a sixtieth each - and stays so. While a race runs, splitscreen's too
 * (its HUD drawn: the menus' backdrops run the race's world too; not with a
 * menu up - the pause's, the race's end), the vertical blanks come twice as
 * often
 * (SetFastForward(2)) - or at the screen's refresh (SetBlankRate; a whole
 * multiple of 60 as SetFastForward's) - and the game draws a picture at
 * each. Its main loop (0x00132600) runs the race's ticks in a loop, as many
 * as the last picture took (0x00376570: one a picture, at least): here as
 * many as 60 a second asks instead - each blank moves the race's time on by
 * 59.94 over the rate (a half at 120), a tick each time that takes it past
 * the next one: 0, 1 or more a picture. What it runs once a frame after
 * them (0x00131F90: a crash's camera, its clock) runs only after a tick.
 *
 * The pictures between: what moves is placed between ticks. After each
 * frame that ticked, every transform of the race's objects (RenderWare's
 * RwMatrix: right, up, at, pos, the first three orthonormal - the cars,
 * their wheels, the traffic, found by shape: engine/tools/find_transforms.py)
 * and the camera's values are taken down; for each picture each one that
 * moved is placed between where the tick before and the last left it, at
 * the picture's time (mods.cpp's Frame, after the ticks, before the draw);
 * at the next frame's start (the main loop's, 0x002225C0) it is put back.
 * So the pictures show the race evenly - at 120 at ticks N - 1/2, N,
 * N + 1/2 - less than a tick late (half of one at 120, 0.58 at 144). One
 * that jumped further than a car moves in a tick (a cut, a respawn) is not
 * blended.
 */

#include <algorithm>
#include <atomic>
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

bool MenuShowing();                     // mouse.cpp
unsigned long long RaceHudCalls();      // controls.cpp
unsigned PictureBlanks();               // sixty_fps.cpp
void F_00376570(lifted::Context& c);
void F_00131F90(lifted::Context& c);
void F_002225C0(lifted::Context& c);

namespace {

// Where the race's transforms live (find_transforms.py, in a race and in a
// crash): the cars, their wheels, the traffic between 0x00517000 and
// 0x00660000. Not a crash's flying parts (about 0x00825000: the movies'
// buffers share that memory - placed there, a movie broke), nor the
// camera's matrices (made anew in the draw: the camera's values below are).
constexpr u32 kObjectsBegin = 0x00500000u, kObjectsEnd = 0x00700000u;
constexpr float kCut = 50.0f;           // further a step than this: a cut, not motion

// The pictures a second while above 60 (0: not), and the steps one picture
// moves the race's time on.
double g_rate = 0.0;
double g_per_picture = 1.0;
// The ticks asked for: each blank adds g_per_picture; past 1 the race's next
// frame ticks, and 1 is taken off (Ticks).
double g_time = 1.0;
// How far the picture is behind the last tick the race ran, from what ran:
// the ticks it ran (its tick count, gp-0x5C5C, counted up in the loop of
// them: 0x00132DC8) less the time the pictures moved on - kept between 0
// and 1 (where the counting began sets no phase). The picture is placed at
// 1 less that, between the last two ticks.
double g_behind = 0.0;
float g_picture_time = 1.0f;
bool g_counted = false;                 // g_tick_count holds the race's (since the rate changed)
u32 g_tick_count = 0;
// The ticks this race frame ran.
u32 g_frame_ticks = 1;
// Blanks since the race's frame last ran (AfterTicks): past kGone, the race
// has gone - to the menus, a movie - and the rate is the game's again.
// (Frame, where the rate is chosen, runs only in the race's frames: left
// there, the menus and movies after a race ran at 120.)
std::atomic<int> g_blanks_since{0};
constexpr int kGone = 30;

struct Matrix {
    float m[16];
};
std::unordered_map<u32, Matrix> g_last, g_now;      // the transforms after the last two ticks
// What the in-between placing covered: as the tick left it, and as placed.
struct Placed {
    u32 address;
    Matrix was, placed;
};
std::vector<Placed> g_placed;

// The cameras: the draw makes each view (0x00227360, three times a
// picture) from its camera's own values, set in the ticks (a crash's camera
// moves on its own) - player 1's at 0x00517030, player 2's (splitscreen)
// 0xBB0 on: the field of view (+0x14), the way it looks (+0x20), where it
// is (+0x30). Placed between ticks as the transforms are, value by value:
// one of 1e-6 to 1e6 either way (a pointer, a count, a flag reads as a far
// smaller float) that moved by less than kCut.
constexpr u32 kCameraValues = 0x00517030u, kCameraStride = 0xBB0u;
constexpr int kCameras = 2, kCameraCount = 16;
struct CameraValues {
    float last[kCameraCount], now[kCameraCount], placed[kCameraCount], was[kCameraCount];
    bool placing = false;               // `placed` is in RAM (PutBack)
};
CameraValues g_cameras[kCameras];
u32 CameraAddress(int which) { return kCameraValues + kCameraStride * u32(which); }

constexpr double kStepRate = 1e6 / 16683.0;    // the blanks' own, 59.94 (threads.cpp's VblankTimer)

/* FRAME RATE, as pictures a second (60: none above the game's own):
   PS2HOST_FRAME_RATE, else the setting - 120, or "display", the screen's
   refresh (up to 240; 60 where it is not known or below 90). */
// What this machine kept up with in this race (0: no limit found): a rate
// whose pictures were often shown late (the window's LatePictures:
// splitscreen at 144 on the author's laptop, one in six) steps down a
// multiple of 60 for the rest of the race. Looked at every two seconds or
// so of race frames, the first such stretch after a change let go (its
// catching up).
double g_cap = 0.0;
int g_sampled = 0;                      // race frames since the last look
int g_looks = 0;                        // looks since the rate changed
u64 g_late_seen = 0;                    // LatePictures at the last look

double Wanted() {
    static const char* env = std::getenv("PS2HOST_FRAME_RATE");
    const std::string rate = env ? std::string(env) : ps2host::Setting("frame_rate", "60");
    const double hz = rate == "display" ? double(ps2host::WindowRefreshRate()) : std::atof(rate.c_str());
    const double want = hz >= 90.0 ? std::min(hz, 240.0) : 60.0;
    return g_cap > 0.0 ? std::min(want, g_cap) : want;
}

/* Each race frame above 60: every two seconds or so, the pictures shown
   late since; more than 5% and the rate steps down (to the multiple of 60
   below it). */
void KeepUp() {
    const int sample = int(2.0 * g_rate);
    if (++g_sampled < sample) return;
    const u64 late_now = ps2host::LatePictures(), late = late_now - g_late_seen;
    g_late_seen = late_now;
    g_sampled = 0;
    if (g_looks++ == 0) return;         // (the change's own catching up)
    if (late * 20 > u64(sample)) {
        g_cap = std::max(60.0, 60.0 * std::ceil(g_rate / 60.0 - 1.0 - 1e-6));
        std::fprintf(stderr, "[high fps] %llu of %d pictures late at %.0f a second: %.0f for this race\n",
                     static_cast<unsigned long long>(late), sample, g_rate, g_cap);
    }
}

/* The blanks at `hz`: a whole multiple of 60 (within a hertz for each 60:
   120, 180, 240) as SetFastForward's multiple, exact; another (144, 165) at
   its own rate. The steps a picture moves the race's time on. */
double Pace(double hz) {
    const int multiple = std::max(int(hz / 60.0 + 0.5), 1);
    if (std::fabs(hz - 60.0 * multiple) < multiple) {
        ps2host::SetBlankRate(0.0);
        ps2host::SetFastForward(unsigned(multiple));
        return 1.0 / multiple;
    }
    ps2host::SetFastForward(1);
    ps2host::SetBlankRate(hz);
    return kStepRate / hz;
}

/* A race running: its HUD drawn within the last few calls (Frame's, a few
   a picture) - the menus' and the intro's backdrops run the race's world
   too, without the HUD; the world itself is no guide, a crash's slow motion
   steps it seldom - and no menu up (the pause's, the race's end: at 60). */
bool Racing() {
    static unsigned long long hud_seen = 0;
    static int hud_still = 99;
    const unsigned long long hud = RaceHudCalls();
    hud_still = hud != hud_seen ? 0 : hud_still + 1;
    hud_seen = hud;
    return hud_still <= 8 && !MenuShowing();
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

/* The transforms as the tick left them. */
void TakeDown() {
    for (int k = 0; k < kCameras; ++k)
        if (const u8* camera = generated::RamPointer(CameraAddress(k), sizeof g_cameras[k].now)) {
            CameraValues& v = g_cameras[k];
            std::memcpy(v.last, v.now, sizeof v.last);
            std::memcpy(v.now, camera, sizeof v.now);
        }
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

/* A transform `t` of the way from a to b: the position between, the
   rotation the rows' blend made orthonormal again (at, then up square to
   it, then right from the two, its handedness the original's). */
Matrix Between(const Matrix& a, const Matrix& b, float t) {
    Matrix out = b;
    float right[3], up[3], at[3];
    const float s = 1.0f - t;
    for (int k = 0; k < 3; ++k) {
        right[k] = s * a.m[k] + t * b.m[k];
        up[k] = s * a.m[4 + k] + t * b.m[4 + k];
        at[k] = s * a.m[8 + k] + t * b.m[8 + k];
        out.m[12 + k] = a.m[12 + k] + t * (b.m[12 + k] - a.m[12 + k]);
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

/* A transform that moved, placed `t` of the way from the tick before to
   the last. */
void Place(u32 address, const Matrix& last, const Matrix& now, float t) {
    float d[3];
    for (int k = 0; k < 3; ++k) d[k] = now.m[12 + k] - last.m[12 + k];
    const float moved = std::sqrt(Dot3(d, d));
    bool turned = false;
    for (int k = 0; k < 12 && !turned; ++k) turned = now.m[k] != last.m[k];
    if ((moved == 0.0f && !turned) || moved > kCut) return;
    u8* p = generated::RamPointer(address, 64);
    if (!p) return;
    Placed placed{address, {}, {}};
    std::memcpy(placed.was.m, p, sizeof placed.was.m);
    placed.placed = placed.was;
    const Matrix between = Between(last, now, t);
    std::memcpy(placed.placed.m, between.m, 12 * 4);     // the rows (their fourth words as they were)
    std::memcpy(placed.placed.m + 12, between.m + 12, 3 * 4);
    std::memcpy(p, placed.placed.m, sizeof placed.placed.m);
    g_placed.push_back(placed);
}

bool AValue(float v) { return std::fabs(v) > 1e-6f && std::fabs(v) < 1e6f; }

/* For the picture drawn now: the camera's values and each transform that
   moved, `t` of the way from the tick before to the last (1: as the last
   left them). */
void PlaceBetween(float t) {
    if (t >= 1.0f || g_last.empty()) return;
    for (int k = 0; k < kCameras; ++k) {
        CameraValues& v = g_cameras[k];
        float* camera = reinterpret_cast<float*>(generated::RamPointer(CameraAddress(k), sizeof v.now));
        if (!camera) continue;
        std::memcpy(v.was, camera, sizeof v.was);
        std::memcpy(v.placed, camera, sizeof v.placed);
        for (int i = 0; i < kCameraCount; ++i) {
            const float a = v.last[i], b = v.now[i];
            if (a == b || !AValue(a) || !AValue(b) || !(std::fabs(b - a) < kCut)) continue;
            v.placed[i] = a + t * (b - a);
        }
        std::memcpy(camera, v.placed, sizeof v.placed);
        v.placing = true;
    }
    for (const auto& [address, now] : g_now) {
        const auto last = g_last.find(address);
        if (last != g_last.end()) Place(address, last->second, now, t);
    }
}

/* What the placing covered, back as the tick left it - where the picture
   left it as placed (what the draw wrote anew stays). */
void PutBack() {
    for (const Placed& placed : g_placed) {
        u8* p = generated::RamPointer(placed.address, 64);
        if (p && std::memcmp(p, placed.placed.m, sizeof placed.placed.m) == 0)
            std::memcpy(p, placed.was.m, sizeof placed.was.m);
    }
    g_placed.clear();
    for (int k = 0; k < kCameras; ++k) {
        CameraValues& v = g_cameras[k];
        if (!v.placing) continue;
        u8* camera = generated::RamPointer(CameraAddress(k), sizeof v.now);
        if (camera && std::memcmp(camera, v.placed, sizeof v.placed) == 0) std::memcpy(camera, v.was, sizeof v.was);
        v.placing = false;
    }
}

bool Blend() {
    static const bool blend = std::getenv("PS2HOST_HIGH_FPS_NO_BLEND") == nullptr;   // to compare
    return blend;
}

}  // namespace

/* Each picture (mods.cpp's Frame: the race's ticks run, its draw next):
   the rate as the race and the setting say (and the screen: a window moved
   to another, fullscreen's mode); above 60 the transforms placed for the
   picture. */
void HighFpsFrame() {
    // Down to 60 only once the race has not run for a quarter of a second or
    // so: a message's moment, a HUD skipped for a few pictures in a crash,
    // stay at the rate (a change drains the GS thread: a hitch).
    constexpr int kSettle = 30;
    static int differing = 0;
    const bool racing = Racing();
    if (!racing) g_cap = 0.0;           // (a new race tries the setting's rate again)
    const double want = racing ? Wanted() : 60.0;
    const double now = g_rate > 0.0 ? g_rate : 60.0;
    differing = std::fabs(want - now) >= 0.5 ? differing + 1 : 0;
    if (differing >= (want > 60.0 ? 1 : kSettle)) {
        differing = 0;
        std::fprintf(stderr, "[high fps] %.0f pictures a second\n", want);
        PutBack();
        g_last.clear();
        g_now.clear();
        g_time = 1.0;
        g_behind = 0.0;
        g_counted = false;
        g_sampled = 0;
        g_looks = 0;
        if (want > 60.0) {
            g_rate = want;
            g_per_picture = Pace(want);
        } else {
            g_rate = 0.0;
            g_per_picture = 1.0;
            ps2host::SetBlankRate(0.0);
            ps2host::SetFastForward(1);
        }
    }
    if (g_rate > 0.0 && Blend()) {
        PutBack();
        PlaceBetween(g_picture_time);
    }
}

namespace {

/* 0x00376570(timer, least, most): the ticks the race's next frame runs -
   the timer's since the last call, at least `least`. Called so by the
   main loop (0x00132600, returning to 0x00132F64 or 0x00132F7C); above
   60 pictures a second, as many as 60 a second asks for the blanks the
   picture took (so a picture late is caught up, as at 60), up to `most`. */
void Ticks(lifted::Context& c) {
    const u32 ra = u32(c.r[31]);
    const s32 most = s32(c.r[6]);
    F_00376570(c);
    if (g_rate <= 0.0 || (ra != 0x00132F64u && ra != 0x00132F7Cu)) return;
    g_time += g_per_picture * std::max(1u, PictureBlanks());
    s32 ticks = 0;
    while (g_time > 1.0 + 1e-6) {
        g_time -= 1.0;
        ++ticks;
    }
    if (ticks > most) {
        ticks = most;
        g_time = 1.0;
    }
    c.r[2] = u64(s64(ticks));
}

/* 0x002225C0, the main loop's first call each frame (0x00132750), before
   the race's ticks: what the last picture placed, back as the ticks left
   it. */
void FrameStart(lifted::Context& c) {
    PutBack();
    F_002225C0(c);
}

/* 0x00131F90(game): the race frame's logic after its ticks, once a frame -
   a crash's: its camera's moves, Impact Time's clock. At 60 a frame is a
   tick; above, a frame that ran no tick does not run it either (run at
   every picture, at 120 the crash went at twice its speed) - and one that
   did has its transforms taken down, for the pictures until the next. Here
   too the ticks the frame ran are counted, and the picture's time found. */
void AfterTicks(lifted::Context& c) {
    g_blanks_since = 0;
    if (g_rate > 0.0) KeepUp();
    const u32 count = generated::ReadU32(u32(c.r[28]) - 0x5C5Cu);
    g_frame_ticks = g_counted ? count - g_tick_count : 1;
    g_tick_count = count;
    g_counted = true;
    if (g_rate > 0.0) {
        g_behind += double(g_frame_ticks) - g_per_picture * std::max(1u, PictureBlanks());
        if (g_behind >= 1.0) g_behind -= std::floor(g_behind);
        if (g_behind < 0.0) g_behind = 0.0;
        g_picture_time = float(1.0 - g_behind);
    } else {
        g_picture_time = 1.0f;
    }
    if (g_rate > 0.0 && g_frame_ticks == 0) return;
    F_00131F90(c);
    if (g_rate > 0.0) TakeDown();
}

}  // namespace

/* Each vertical blank's start (sixty_fps.cpp's BlankHandler, in every mode
   of the game): above 60, back to 60 once the race's frames have stopped.
   What was placed is left (FrameStart put it back; the memory may be
   another's by now). */
void HighFpsBlank() {
    if (g_rate <= 0.0 || ++g_blanks_since <= kGone * std::max(1, int(g_rate / 60.0 + 0.5))) return;
    std::fprintf(stderr, "[high fps] 60 pictures a second (the race's frames stopped)\n");
    g_cap = 0.0;
    g_placed.clear();
    for (CameraValues& v : g_cameras) v.placing = false;
    g_last.clear();
    g_now.clear();
    g_rate = 0.0;
    g_per_picture = 1.0;
    g_time = 1.0;
    g_behind = 0.0;
    g_counted = false;
    ps2host::SetBlankRate(0.0);
    ps2host::SetFastForward(1);
}

void RegisterHighFps() {
    ps2host::Override(0x00376570u, Ticks, "the race's ticks a frame: 60 a second at 120 pictures and more");
    ps2host::Override(0x002225C0u, FrameStart, "the main loop's frame starts: the pictures' placing put back");
    ps2host::Override(0x00131F90u, AfterTicks, "the race frame's logic after its ticks: once a tick at 120 and more");
}
