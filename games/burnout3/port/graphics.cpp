/*
 * PC SETTINGS' GRAPHICS: the game's own effects, each as the game draws it
 * (ORIGINAL), turned off, or changed (docs/GRAPHICS_PLAN.md).
 *
 * The race's render (0x00276300) draws, per view, the fog (0x002419C0), the
 * glow (CB3Bloom::Render, 0x002659C0), then the camera's directional blur
 * (0x00426C10) or the boost's radial blur (0x002FA9A0), and the shadows
 * (CB3ShadowMgr::RenderShadows, 0x003033F0) before them. The graphics
 * manager (0x00665EC0) has a byte for each - its debug menu's switches
 * (named by Nehalem's B3Mod), all 1 from its set-up (0x0019EBF0), which
 * nothing else writes: bloom +0x68, blur +0x6A (both blurs), shadows +0x67.
 * They are written here at the display's frame step (mods.cpp's Frame calls
 * GraphicsFrame), only while a setting is not ORIGINAL; the 1 back once it
 * is again.
 *
 *   AMBIENT OCCLUSION  ON / OFF - shade where things meet (Occlusion)
 *   LIGHTING        MODERN / ORIGINAL - a grade of the picture (Grade)
 *   BLOOM           ORIGINAL / MODERN / HIGH / OFF (LOW, a setting's still)
 *                   - MODERN the PC's glow (ModernBloom), HIGH and LOW the
 *                   glow's threshold, intensity and trails changed
 *   MOTION BLUR     ORIGINAL / MODERN / OFF (the key "blur", as it was
 *                   saved) - MODERN the camera's own (CameraBlur)
 *   SHADOWS         ORIGINAL / MODERN / SOFT / OFF - SOFT the blob drawn
 *                   here (see SoftBlob), MODERN it and the sun's
 *   REFLECTIONS     MODERN / ORIGINAL - the paint's environment map sharp
 *                   and by a Fresnel term
 *   TEXTURES        ORIGINAL / MODERN - trilinear, anisotropic, the level
 *                   from the screen (ORIGINAL by default)
 *   MODERN by default but TEXTURES (GraphicsDefault).
 *   LOD             ORIGINAL / HIGH - the cars' and the traffic's detailed
 *                   models out to longer distances (see the LOD distances)
 *   PARTICLES       ORIGINAL / MORE / MOST - Nehalem's patch, lifted with the
 *                   MODS' (port/tools/lift_mods.py)
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "ps2host/texture_pack.h"

// The glow, the radial blur, the directional blur, the fog, the packets'
// room (see BloomRender, RadialBlur, Fog, EmitEffect).
void F_002659C0(lifted::Context& c);
void F_002FA9A0(lifted::Context& c);
void F_00426C10(lifted::Context& c);
void F_002419C0(lifted::Context& c);
void F_001F3080(lifted::Context& c);

namespace {

namespace pack = ps2host::texture_pack;

constexpr u32 kGraphicsManager = 0x00665EC0u;
constexpr u32 kShadowsOn = kGraphicsManager + 0x67u, kBloomOn = kGraphicsManager + 0x68u,
              kBlurOn = kGraphicsManager + 0x6Au;

/* A switch byte: 0 while its setting turns the effect off, the game's 1
   back once when it no longer does. */
struct Switch {
    u32 address;
    bool cleared = false;
    void Set(bool off) {
        if (off) {
            generated::Write8(address, 0);
            cleared = true;
        } else if (cleared) {
            generated::Write8(address, 1);
            cleared = false;
        }
    }
};
Switch g_bloom{kBloomOn}, g_blur{kBlurOn}, g_shadows{kShadowsOn};

bool Is(const char* key, const char* fallback, const char* value) {
    return ps2host::Setting(key, fallback) == value;
}

}  // namespace

/* The graphics settings' defaults: the PC's look, MODERN, unless the
   "graphics" setting is "original" - the game's own pictures by default
   (the recorded runs' --setting graphics=original). */
/* An integrated GPU (a laptop's Intel UHD, Iris, an AMD APU's Radeon): the
   effects that cost it most - AO and the sun's shadows, the camera's blur -
   off by default there; the glow and the grade stay (MODERN's glow is
   lighter than the game's). */
bool IntegratedGpu() {
    static int known = -1;              // (once the GPU is started)
    if (known >= 0) return known != 0;
    const std::string name = ps2host::GpuName();
    if (name.empty()) return false;
    known = 0;
    for (const char* weak : {"UHD", "Iris", "HD Graphics", "Radeon(TM) Graphics", "Radeon Graphics", "Vega"})
        if (name.find(weak) != std::string::npos) known = 1;
    return known != 0;
}

const char* GraphicsDefault(const char* key) {
    static const bool original = ps2host::Setting("graphics", "modern") == "original";
    const std::string k = key;
    const bool light = IntegratedGpu();
    if (k == "ao") return original || light ? "off" : "on";
    if (light && k == "shadows") return original ? "original" : "soft";
    if (light && k == "blur") return "original";
    // (TEXTURES: ORIGINAL - the game's own level choice is already sharp;
    // MODERN smooths its bands in motion, at a cost)
    if (k == "bloom" || k == "blur" || k == "shadows" || k == "lighting" || k == "reflections")
        return original ? "original" : "modern";
    return "original";
}

namespace {

std::string Graphics(const char* key) { return ps2host::Setting(key, GraphicsDefault(key)); }

// ---------------------------------------------------------------- the PC's effects
//
// MODERN effects run on the GPU in the game's own frame (gs_renderer.h's
// GsEffect): written into the game's packets as GS register writes no GS
// has (0x70-0x7E), at the point of the frame the effect belongs - ambient
// occlusion before the fog, the glow and the grade where the game's glow
// is, the blur where its blurs are - so they work on the 3D alone, never
// the HUD drawn after.

/* gs_renderer.h's GsEffectId. */
enum class EffectId : u32 { kTint = 0xF0, kOcclusion = 1, kMotionBlur = 2, kBloom = 3, kGrade = 4 };

/* The view being drawn: the graphics manager's per-view block (0x1C0 each,
   the view's index at 0x00667050), as the fog reads it - FRAME +0x630,
   ZBUF +0x640, and the view's rectangle as floats (width +0x688, height +0x68C,
   x +0x690, y +0x694; FRAME the whole picture's, player 2 224 rows
   down it). */
struct View {
    u32 index = 0;
    u64 frame = 0, zbuf = 0;
    u32 x = 0, y = 0, w = 0, h = 0;
};

float F32(u32 address) {
    const u32 bits = generated::ReadU32(address);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

/* A view of a split screen (two players): the graphics manager's block is
   one for both (its index 0), and the camera RenderWare has while a view's
   effects are written is the one the last view was drawn with. The view's
   player is told by its rectangle (player 2's below or right of player 1's),
   its camera by the player's own values (CameraValues). */
bool Split(const View& v) { return v.h < 400 || v.w < 600; }
int PlayerOf(const View& v) { return v.x > 0 || v.y > 0 ? 1 : 0; }

View CurrentView() {
    View v;
    v.index = generated::ReadU32(0x00667050u);
    const u32 base = kGraphicsManager + v.index * 0x1C0u;
    v.frame = generated::ReadU64(base + 0x630u);
    v.zbuf = generated::ReadU64(base + 0x640u);
    v.w = u32(std::max(0.0f, F32(base + 0x688u)));
    v.h = u32(std::max(0.0f, F32(base + 0x68Cu)));
    v.x = u32(std::max(0.0f, F32(base + 0x690u)));
    v.y = u32(std::max(0.0f, F32(base + 0x694u)));
    return v;
}

/* The RenderWare camera the view is drawn with: the last one given a view
   window (widescreen.cpp's SetViewWindow tells NoteCamera), by the view.
   RwCamera: its frame +0x04 (RwObject's parent), view window +0x68,
   near +0x80, far +0x84, zScale +0x8C, zShift +0x90. */
u32 g_cameras[2] = {};
// (One camera serves both views in splitscreen, placed for each in turn.)
u32 CameraOf(u32 view) { return g_cameras[view & 1] ? g_cameras[view & 1] : g_cameras[0]; }

struct Camera {
    bool valid = false;
    float window_x = 1, window_y = 0.75f;
    float near_plane = 1, far_plane = 1000, z_scale = 0, z_shift = 0;
    float right[3] = {}, up[3] = {}, at[3] = {}, pos[3] = {};      // its frame's LTM (+0x50)
};

Camera ReadCamera(u32 camera) {
    Camera k;
    if (!camera) return k;
    k.window_x = F32(camera + 0x68u);
    k.window_y = F32(camera + 0x6Cu);
    k.near_plane = F32(camera + 0x80u);
    k.far_plane = F32(camera + 0x84u);
    k.z_scale = F32(camera + 0x8Cu);
    k.z_shift = F32(camera + 0x90u);
    // Its place from the view matrix (+0x20, rows of a 3x3 and the
    // translation at +0x50), which RenderWare makes for the view being drawn
    // (RwCameraBeginUpdate) - not from its frame: splitscreen's one camera
    // is placed for both views before either is drawn. The matrix takes a
    // world point to the camera's, its columns the screen's right and down
    // (scaled, and mixed with "at" for the window's offset) and "at".
    float m[3][3], t[3];
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; i < 3; ++i) m[r][i] = F32(camera + 0x20u + 16u * r + 4u * i);
        t[r] = F32(camera + 0x50u + 4u * r);
    }
    auto column = [&](int i, float* out) {
        for (int r = 0; r < 3; ++r) out[r] = m[r][i];
    };
    auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto normalise = [&](float* a) {
        const float l = std::sqrt(dot(a, a));
        if (l > 1e-6f)
            for (int i = 0; i < 3; ++i) a[i] /= l;
        return l > 1e-6f;
    };
    float x[3], y[3];
    column(2, k.at);
    column(0, x);
    column(1, y);
    bool ok = normalise(k.at);
    const float xa = dot(x, k.at), ya = dot(y, k.at);
    for (int i = 0; i < 3; ++i) {
        x[i] -= xa * k.at[i];
        y[i] -= ya * k.at[i];
    }
    ok = normalise(x) && normalise(y) && ok;
    for (int i = 0; i < 3; ++i) {
        k.right[i] = -x[i];             // (as the frame has it: RenderWare's "right" points left)
        k.up[i] = -y[i];
    }
    // The camera where the matrix gives 0: M^T p = -t.
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
                      m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) +
                      m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
    if (std::fabs(det) > 1e-9f) {
        // p = -(M^T)^-1 t: Cramer's rule on the rows of M^T (M's columns).
        float a[3][3];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) a[r][c] = m[c][r];
        const float b[3] = {-t[0], -t[1], -t[2]};
        auto det3 = [](const float (*q)[3]) {
            return q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) - q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0]) +
                   q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]);
        };
        const float d = det3(a);
        for (int c = 0; c < 3; ++c) {
            float q[3][3];
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 3; ++j) q[r][j] = j == c ? b[r] : a[r][j];
            k.pos[c] = det3(q) / d;
        }
    } else {
        ok = false;
    }
    k.valid = ok && k.window_x > 0.0f && k.window_y > 0.0f;
    return k;
}

/* A player's camera from the values the game builds its view from (player
   1's at 0x00517030, player 2's 0xBB0 on): its orientation a quaternion
   (+0x20: x, y, z, w; its rotation's columns RenderWare's right, up and at),
   its place +0x30 - the view's to within a degree and a few centimetres. The
   window and depth's mapping are the RwCamera's (`k`). */
Camera CameraValues(const Camera& k, int player) {
    Camera v = k;
    const u32 base = 0x00517030u + 0xBB0u * u32(player & 1);
    const float x = F32(base + 0x20u), y = F32(base + 0x24u), z = F32(base + 0x28u), w = F32(base + 0x2Cu);
    v.right[0] = 1 - 2 * (y * y + z * z);
    v.right[1] = 2 * (x * y + z * w);
    v.right[2] = 2 * (x * z - y * w);
    v.up[0] = 2 * (x * y - z * w);
    v.up[1] = 1 - 2 * (x * x + z * z);
    v.up[2] = 2 * (y * z + x * w);
    v.at[0] = 2 * (x * z + y * w);
    v.at[1] = 2 * (y * z - x * w);
    v.at[2] = 1 - 2 * (x * x + y * y);
    for (int i = 0; i < 3; ++i) v.pos[i] = F32(base + 0x30u + 4u * i);
    const float n = x * x + y * y + z * z + w * w;
    v.valid = k.valid && n > 0.9f && n < 1.1f;
    return v;
}

/* The camera a view is drawn with: the RwCamera's view matrix, or in a
   split screen its player's values. */
Camera ViewCamera(const View& v) {
    const Camera k = ReadCamera(CameraOf(0));
    return Split(v) ? CameraValues(k, PlayerOf(v)) : k;
}

/* Writes an effect into the game's packets: the room for a GIF tag and 15
   A+D writes taken as the glow takes its own (0x001F3080: the packets'
   cursor at gp-0x5EB0), then the 14 parameters (0x70-0x7D) and the run
   (0x7E). Every register of the caller's kept: it is called from inside
   the game's functions, before their own code runs. */
void EmitEffect(lifted::Context& c, EffectId id, const View& v, const float* params, int count) {
    const lifted::Context saved = c;
    c.r[4] = 0x80000000u;
    c.r[5] = 16;
    F_001F3080(c);
    const u32 gp = u32(saved.r[28]);
    const u32 cursor = generated::ReadU32(gp - 0x5EB0u);
    auto qword = [&](u32 at, u64 low, u64 high) {
        generated::Write64(at, low);
        generated::Write64(at + 8, high);
    };
    qword(cursor, 15u | (1u << 15) | (1ull << 60), 0xE);       // NLOOP 15, EOP, A+D
    u64 p[14] = {};
    p[0] = v.frame;
    p[1] = v.zbuf;
    p[2] = u64(v.x) | u64(v.y) << 16 | u64(v.x + v.w) << 32 | u64(v.y + v.h) << 48;
    for (int i = 0; i < count && i < 22; ++i) {
        u32 bits;
        std::memcpy(&bits, &params[i], 4);
        p[3 + i / 2] |= u64(bits) << (32 * (i & 1));
    }
    for (u32 i = 0; i < 14; ++i) qword(cursor + 16 * (1 + i), p[i], 0x70 + i);
    qword(cursor + 16 * 15, u64(id), 0x7E);
    generated::Write32(gp - 0x5EB0u, cursor + 16 * 16);
    const s64 budget = c.budget;
    const u32 attention = c.attention;
    c = saved;
    c.budget = budget;
    c.attention = attention;
}

u32 g_frames = 0;                // GraphicsFrame's calls (a few a picture)
// Each view's pictures (the fog's calls: one a view a picture), and the
// picture each view's blur was last sent in.
u32 g_pictures[2] = {};
u32 g_blurred[2] = {~0u, ~0u};

/* The depth's mapping and the view window, the first four parameters of
   the effects that place pixels in view space: Z = zScale / w + zShift. */
void DepthParams(const Camera& k, float* p) {
    p[0] = k.z_scale;
    p[1] = k.z_shift;
    p[2] = k.window_x;
    p[3] = k.window_y;
}

/* The view's player's car in the view's space - its place in the race's
   block (player 1's 0x0064E710, player 2's 0x27D0 on) - the centre of its
   body, when it is in front of the camera as a chase camera has it. */
bool CarInView(const Camera& k, int view, float* out) {
    const u32 at = 0x0064E710u + 0x27D0u * u32(view & 1);
    const float car[3] = {F32(at) - k.pos[0], F32(at + 4u) - k.pos[1], F32(at + 8u) - k.pos[2]};
    const float* rows[3] = {k.right, k.up, k.at};
    const float sign[3] = {-1.0f, 1.0f, 1.0f};      // (RenderWare's "right" points left on screen)
    for (int i = 0; i < 3; ++i) out[i] = sign[i] * (rows[i][0] * car[0] + rows[i][1] * car[1] + rows[i][2] * car[2]);
    out[1] += 0.3f;
    out[2] -= 0.4f;
    return out[2] > 1.1f && out[2] < 15.0f && std::fabs(out[0]) < 6.0f;
}

/* AO (ambient occlusion), before the fog: the corners, the cars' contact
   with the road, the walls' feet. */
void Occlusion(lifted::Context& c) {
    const View v = CurrentView();
    const Camera k = ViewCamera(v);
    if (!k.valid || !v.zbuf) return;
    float p[22] = {};
    DepthParams(k, p);
    p[4] = 1.5f;            // radius, world units
    p[5] = Graphics("ao") == "on" ? 0.75f : 0.0f;   // strength
    p[6] = 180.0f;          // gone by this distance
    // SHADOWS MODERN: the sun's shadows through the depth buffer - a sun
    // high in the sky (the world's y up), its direction into the view's.
    if (Graphics("shadows") == "modern") {
        float sun[3] = {0.35f, 0.85f, -0.40f};
        const float length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
        for (float& x : sun) x /= length;
        p[7] = -(k.right[0] * sun[0] + k.right[1] * sun[1] + k.right[2] * sun[2]);
        p[8] = k.up[0] * sun[0] + k.up[1] * sun[1] + k.up[2] * sun[2];
        p[9] = k.at[0] * sun[0] + k.at[1] * sun[1] + k.at[2] * sun[2];
        p[10] = 0.5f;       // strength
        p[11] = 15.0f;      // the longest shadow, metres
    }
    // The player's car (p12-15: its centre, view space, and size): no sun's
    // shadow of its own on it, and less occlusion - its paint keeps its
    // gloss; what it casts on the road stays.
    if (CarInView(k, PlayerOf(v), &p[12])) p[15] = 2.6f;
    EmitEffect(c, EffectId::kOcclusion, v, p, 22);
}

/* MOTION BLUR MODERN: the camera's own motion, a frame's, the boost making
   the shutter longer. `boost` 0-1. */
struct Motion {
    Camera last;
    u32 picture = ~0u;
    u64 blank = 0;              // ps2host::FrameNumber() then
};
Motion g_motion[2];

void CameraBlur(lifted::Context& c, float boost) {
    const View v = CurrentView();
    const int view = PlayerOf(v);
    if (g_blurred[view] == g_pictures[view]) return;    // (the other blur sent it)
    g_blurred[view] = g_pictures[view];
    const Camera k = ViewCamera(v);
    Motion& m = g_motion[view];
    const u64 blank = ps2host::FrameNumber();
    const bool have_last = m.last.valid && m.picture + 1 == g_pictures[view] && blank > m.blank && blank - m.blank < 8;
    const Camera last = have_last ? m.last : k;
    // The motion over the shutter's time: the picture's own (over the blanks
    // since the last), so that the blur is the same at any frame rate.
    const double elapsed = have_last ? double(blank - m.blank) * double(ps2host::BlankPeriodNs()) * 1e-9 : 1.0;
    m.last = k;
    m.picture = g_pictures[view];
    m.blank = blank;
    if (!k.valid || !v.zbuf) return;
    // The view's basis as rows (RenderWare's "right" points left on screen).
    auto basis = [](const Camera& a, float (*b)[3]) {
        for (int i = 0; i < 3; ++i) {
            b[0][i] = -a.right[i];
            b[1][i] = a.up[i];
            b[2][i] = a.at[i];
        }
    };
    float bc[3][3], bp[3][3];
    basis(k, bc);
    basis(last, bp);
    const float d[3] = {k.pos[0] - last.pos[0], k.pos[1] - last.pos[1], k.pos[2] - last.pos[2]};
    float p[22] = {};
    DepthParams(k, p);
    float turn = 0.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            p[4 + 4 * i + j] = bp[i][0] * bc[j][0] + bp[i][1] * bc[j][1] + bp[i][2] * bc[j][2];
        p[4 + 4 * i + 3] = bp[i][0] * d[0] + bp[i][1] * d[1] + bp[i][2] * d[2];
        turn += p[4 + 4 * i + i];
    }
    // A cut (a replay's camera, a crash's): no blur across it. Else the
    // motion scaled to the shutter: a 100th of a second, a 50th boosting.
    const float moved = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const double shutter = (1.0 + std::clamp(boost, 0.0f, 1.0f)) / 100.0;
    const float scale = float(std::min(shutter / std::max(elapsed, 1e-3), 4.0));
    // (the camera's turning at half its share: a chase camera swings, and a
    // whole picture smeared by it reads as a fault, not as speed)
    for (int i = 0; i < 12; ++i) {
        const float identity = (i % 5 == 0) ? 1.0f : 0.0f;
        const float share = (i % 4 == 3) ? scale : 0.5f * scale;
        p[4 + i] = (moved > 8.0f || turn < 2.9f) ? identity : identity + share * (p[4 + i] - identity);
    }
    p[16] = 1.0f;                                              // (a gain on it)
    p[17] = 0.05f;                                             // the most, a share of the height
    // The player's car (its place in the race's block, 0x0064E710; player
    // 2's not known: none), a sphere around it kept sharp - only where it is
    // in front of the camera as a chase camera has it.
    if (CarInView(k, view, &p[18])) p[21] = 2.6f;
    EmitEffect(c, EffectId::kMotionBlur, v, p, 22);
}

/* LIGHTING MODERN's grade, the parameters 12-16 of the glow and the grade. */
void GradeParams(float* p) {
    p[9] = 0.5f;            // the picture's white: its colours are halved (the frame's copy doubles them)
    p[12] = 1.0f;           // the bright end's shoulder
    p[13] = 1.04f;          // contrast
    p[14] = 1.08f;          // saturation
    p[15] = 0.15f;          // vignette
    p[16] = 0.01f;          // warmth
}
bool ModernLighting() { return Graphics("lighting") == "modern"; }
void NeutralGrade(float* p) {
    p[9] = 0.5f;
    p[12] = 0.0f;
    p[13] = 1.0f;
    p[14] = 1.0f;
    p[15] = 0.0f;
    p[16] = 0.0f;
}

/* BLOOM MODERN: the glow of the PC's, from the track's threshold and
   intensity (bloom a0: +0, +4), at the internal resolution. */
void ModernBloom(lifted::Context& c) {
    const View v = CurrentView();
    const u32 bloom = u32(c.r[4]);
    const float threshold = F32(bloom), intensity = F32(bloom + 4u);
    float p[22] = {};
    // (in the picture's white: the track's threshold, 0.75 of the halved
    // colours' range, is 1.5 times white)
    p[0] = std::clamp(2.0f * threshold - 0.9f, 0.45f, 1.6f);
    p[1] = 0.55f * std::clamp(intensity, 0.0f, 2.0f);
    p[2] = 0.35f;
    if (ModernLighting()) GradeParams(p);
    else NeutralGrade(p);
    EmitEffect(c, EffectId::kBloom, v, p, 22);
}

void Grade(lifted::Context& c) {
    const View v = CurrentView();
    float p[22] = {};
    GradeParams(p);
    EmitEffect(c, EffectId::kGrade, v, p, 22);
}

/* CB3Bloom::Render(bloom, hold, reset): a0 the view's bloom (+0x50 of the
   view's effects, P1 0x00666090, P2 0x00666130), set each frame by
   0x0030A9D0 from the track's tweaks - threshold +0 (0.75: the bright pass
   takes what is above 0xBF), intensity +4 (1.0: the glow's grey as it is
   added back) and persistence +8 (0.5, up to 0.9 boosting: the share of the
   frame before kept, the glow's trails). BLOOM HIGH and LOW scale them for
   this draw, the game's own values back after it. */
void BloomRender(lifted::Context& c) {
    const std::string strength = Graphics("bloom");
    // (the glow's switch: OFF draws none, and no grade of the glow's)
    if (strength == "modern" && generated::ReadU8(kBloomOn)) {
        ModernBloom(c);
        return;
    }
    if (strength != "low" && strength != "high") {
        F_002659C0(c);
        if (ModernLighting()) Grade(c);
        return;
    }
    const u32 bloom = u32(c.r[4]);
    u32 saved[3];
    float value[3];
    for (int i = 0; i < 3; ++i) {
        saved[i] = generated::ReadU32(bloom + 4u * i);
        std::memcpy(&value[i], &saved[i], 4);
    }
    if (strength == "low") {
        value[0] = std::min(value[0] + 0.05f, 0.99f);
        value[1] *= 0.55f;
        value[2] *= 0.6f;
    } else {   // high
        value[0] = std::max(value[0] - 0.2f, 0.0f);
        value[1] *= 1.5f;
    }
    for (int i = 0; i < 3; ++i) {
        u32 bits;
        std::memcpy(&bits, &value[i], 4);
        generated::Write32(bloom + 4u * i, bits);
    }
    F_002659C0(c);
    for (int i = 0; i < 3; ++i) generated::Write32(bloom + 4u * i, saved[i]);
    if (ModernLighting()) Grade(c);
}

/* 0x002FA9A0: the radial blur, the race's (the blur byte gates it there)
   and the frontend's radial-blur objects' (0x003D2FD0, which the byte does
   not): MOTION BLUR OFF skips it for those too. */
void RadialBlur(lifted::Context& c) {
    const std::string blur = Graphics("blur");
    if (blur == "off") return;
    // MODERN in the race (the frontend's radial blurs have no 3D view of
    // their own: the game's, there).
    if (blur == "modern" && CurrentView().zbuf && generated::ReadU8(kBlurOn)) {
        CameraBlur(c, F32(u32(c.r[4]) + 0x1Cu));
        return;
    }
    F_002FA9A0(c);
}

/* 0x00426C10: the camera's directional blur (the race's, instead of the
   radial one when the camera asks): MODERN's blur in its place. */
void DirectionalBlur(lifted::Context& c) {
    if (Graphics("blur") == "modern" && CurrentView().zbuf) {
        CameraBlur(c, 0.0f);
        return;
    }
    F_00426C10(c);
}

/* 0x002419C0: the fog, AO before it. */
bool g_split = false;            // the views drawn last split (Split)

void Fog(lifted::Context& c) {
    ++g_pictures[PlayerOf(CurrentView())];
    g_split = Split(CurrentView());
    if (Graphics("ao") == "on" || Graphics("shadows") == "modern") Occlusion(c);
    F_002419C0(c);
}

/* The shadows: a blob under each car ("blobbyshadow", 64x64 PSMT8, black,
   its shape in alpha), on an oriented quad whose strip maps V 0-0.1875 and
   0.8125-1 to the car's ends and stretches the middle with its length
   (0x00303C10). SOFT draws it from this one, made here (no art of the
   game's): four times as fine, the same footprint - the ends' falloff over
   their 12 texels, the sides darker at the wheels - its edges a smooth
   penumbra instead of a texel or two. */
constexpr char kBlobName[] = "9c6418cb1b02ab39-19d6ca8581b702a1-00001993";

float Smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::shared_ptr<const pack::Image> SoftBlob() {
    constexpr u32 kSize = 256;
    constexpr float kTexel = 64.0f / kSize;     // in the original's texels
    std::vector<u32> rgba(kSize * kSize);
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const float u = (x + 0.5f) * kTexel, v = (y + 0.5f) * kTexel;   // 0..64
            const float side = std::min(u, 64.0f - u), end = std::min(v, 64.0f - v);
            // The middle, between the wheels, a lighter side than at them.
            const float between = Smooth(14.0f, 22.0f, end);
            const float side_width = 5.0f + 9.0f * between;
            const float edge = Smooth(0.0f, side_width, side) * Smooth(0.0f, 12.0f, end);
            const float alpha = 127.0f * edge * (1.0f - 0.15f * between * (1.0f - Smooth(8.0f, 20.0f, side)));
            rgba[y * kSize + x] = u32(std::lround(alpha)) << 24;
        }
    }
    return pack::MakeImage(std::move(rgba), kSize, kSize);
}

/* The LOD distances (0x00665F30..0x00665F40), from the table at 0x004B72A0
   {15, 30, 60, 20, 80} as a race is set up (0x0019E9A0): a race car's
   model the most detailed (3) within the first, then 2, 1, 0 beyond the
   third (0x00151A40), the traffic's by the last two (0x003078F0), each
   times the camera's own factor. LOD HIGH: each kDetailScale times as far;
   ORIGINAL the table's back. */
constexpr u32 kLodDistances = 0x00665F30u, kLodTable = 0x004B72A0u, kLodCount = 5;
constexpr float kDetailScale = 2.5f;
bool g_detail_set = false;

void SetDetail(bool high) {
    if (!high && !g_detail_set) return;
    for (u32 i = 0; i < kLodCount; ++i) {
        u32 bits = generated::ReadU32(kLodTable + 4u * i);
        if (high) {
            float d;
            std::memcpy(&d, &bits, 4);
            d *= kDetailScale;
            std::memcpy(&bits, &d, 4);
        }
        generated::Write32(kLodDistances + 4u * i, bits);
    }
    g_detail_set = high;
}

std::string g_shadows_now;

}  // namespace

/* At the display's frame step (mods.cpp), a few times a frame. */
void GraphicsFrame() {
    ++g_frames;
    // BLOOM's HIGH and LOW were BLOOM STRENGTH's (a playtest build's): its
    // value, with BLOOM ORIGINAL, is BLOOM's - once, the settings all read.
    static bool migrated = false;
    if (!migrated) {
        migrated = true;
        const std::string strength = ps2host::Setting("bloom_strength", "");
        if (!strength.empty()) {
            if ((strength == "low" || strength == "high") && Is("bloom", "original", "original"))
                ps2host::SetSetting("bloom", strength);
            ps2host::SetSetting("bloom_strength", "");
        }
    }
    g_bloom.Set(Graphics("bloom") == "off");
    g_blur.Set(Graphics("blur") == "off");
    const std::string shadows = Graphics("shadows");
    g_shadows.Set(shadows == "off");
    if (shadows != g_shadows_now) {
        static std::shared_ptr<const pack::Image> soft;
        // (MODERN: the soft blob for the contact, the sun's shadows besides)
        const bool soft_now = shadows == "soft" || shadows == "modern";
        const bool soft_before = g_shadows_now == "soft" || g_shadows_now == "modern";
        if (soft_now) {
            if (!soft) soft = SoftBlob();
            pack::SetOverride(kBlobName, soft);
        } else if (soft_before) {
            pack::SetOverride(kBlobName, std::shared_ptr<const pack::Image>());
        }
        g_shadows_now = shadows;
    }
    SetDetail(Is("lod", "original", "high"));
    // REFLECTIONS MODERN: the cars' environment map - its three faces (FBP
    // 0x18B, 0x193, 0x19B, their Z 0x16B-0x17B) and the sphere map warped
    // from them (FBP 0x16B, 256 wide: TBP 0x2D60 as the paint samples it) -
    // at the internal resolution, and the paint's reflection by a Fresnel
    // term (ps2host::SetGpuDrawTag).
    // (one player only: a split screen's own buffers lie in the same pages,
    // its blurs read them at the PS2's size)
    const bool modern_reflections = Graphics("reflections") == "modern" && !g_split;
    ps2host::SetGpuScaleRule(modern_reflections ? 0x16Bu : 0u, modern_reflections ? 0x1A7u : 0u, modern_reflections ? 4u : 0u);
    ps2host::SetGpuDrawTag(0x2D60u, modern_reflections ? 1u : 0u);
    // TEXTURES MODERN: mipmapped textures filtered as a modern GPU does -
    // their detail at the internal resolution, no bands, the road sharp.
    ps2host::SetGpuModernTextures(Graphics("textures") == "modern");
    // debug.switches_off=<offsets, hex, comma-separated> (a scripted run's
    // --setting, to measure what each effect costs): those switch bytes of
    // the graphics manager 0 - 5F the track, 60 track into the envmap, 62
    // instances into it, 67 shadows, 68 bloom, 69 fog, 6A blur, 6C particles.
    static const std::string off = ps2host::Setting("debug.switches_off", "");
    for (std::size_t at = 0; at < off.size();) {
        const std::size_t comma = std::min(off.find(',', at), off.size());
        const u32 offset = u32(std::strtoul(off.substr(at, comma - at).c_str(), nullptr, 16));
        if (offset && offset < 0x100u) generated::Write8(kGraphicsManager + offset, 0);
        at = comma + 1;
    }
}

void RegisterGraphics() {
    ps2host::Override(0x002659C0u, BloomRender, "the glow: BLOOM HIGH and LOW (PC SETTINGS)");
    ps2host::Override(0x002FA9A0u, RadialBlur, "the radial blur: MOTION BLUR (PC SETTINGS)");
    ps2host::Override(0x00426C10u, DirectionalBlur, "the directional blur: MOTION BLUR MODERN (PC SETTINGS)");
    ps2host::Override(0x002419C0u, Fog, "the fog: AO before it (PC SETTINGS)");
}

/* widescreen.cpp's SetViewWindow: the camera a view is drawn with, its
   window just given - the view's (the graphics manager's view index then).
   PS2_FX_LOG=1 tells each camera's mapping. */
void NoteCamera(u32 camera) {
    const u32 view = generated::ReadU32(0x00667050u) & 1;
    g_cameras[view] = camera;
    static const bool log = std::getenv("PS2_FX_LOG") != nullptr;
    static int told = 0;
    static u32 told_frame = 0;
    if (log && told < 80 && g_frames - told_frame >= 120) {
        ++told;
        told_frame = g_frames;
        const Camera k = ReadCamera(camera);
        std::fprintf(stderr, "[fx] view %u camera %08X window %.4f %.4f near %.3f far %.3f z %.6g %.6g "
                             "at %.3f %.3f %.3f pos %.2f %.2f %.2f\n",
                     unsigned(view), unsigned(camera), k.window_x, k.window_y, k.near_plane, k.far_plane, k.z_scale,
                     k.z_shift, k.at[0], k.at[1], k.at[2], k.pos[0], k.pos[1], k.pos[2]);
        std::fprintf(stderr, "[fx]   values +20 %.2f %.2f %.2f %.2f +30 %.2f %.2f %.2f %.2f car %.2f %.2f %.2f\n",
                     F32(0x00517050u), F32(0x00517054u), F32(0x00517058u), F32(0x0051705Cu), F32(0x00517060u),
                     F32(0x00517064u), F32(0x00517068u), F32(0x0051706Cu), F32(0x00539D90u), F32(0x00539D94u),
                     F32(0x00539D98u));
    }
}
