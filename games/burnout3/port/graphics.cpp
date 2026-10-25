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
 *   BLOOM           ORIGINAL / OFF / MODERN HIGH / MODERN LOW - the PC's
 *                   glow (ModernBloom), LOW's fainter and from brighter
 *                   lights ("modern", "modern_low")
 *   MOTION BLUR     ORIGINAL / OFF / MODERN HIGH / MODERN LOW - the
 *                   camera's own (CameraBlur), LOW's streaks half as long
 *                   ("modern", "modern_low")
 *   SHADOWS         ORIGINAL / MODERN / SOFT / OFF - SOFT the blob drawn
 *                   here (see SoftBlob), MODERN it and the sun's
 *   REFLECTIONS     ORIGINAL / MODERN / SSR LOW / SSR HIGH - the paint's
 *                   environment map sharp and by a Fresnel term; SSR adds what the picture
 *                   shows around the car where the paint's rays find it
 *                   (Reflections: the effects' screen-space reflections)
 *   CINEMATIC CAMERAS  MODERN / ORIGINAL - depth of field in the takedown
 *                   and crash cameras (CinematicFocus)
 *   SMOKE AND FIRE  MODERN / ORIGINAL - soft particles, faded where they
 *                   meet the road and the cars
 *   TEXTURE FILTERING  ORIGINAL / MODERN / NEAREST / BILINEAR / TRILINEAR /
 *                   ANISOTROPIC 4X, 8X, 16X (the key "textures"; ORIGINAL by
 *                   default) - MODERN the game's own filters with mipmapped
 *                   textures trilinear and anisotropic by the screen; the
 *                   rest the player's filter forced on the 3D (not on
 *                   sprites - the HUD, text, the picture's own copies - nor
 *                   on a texture drawn into: ps2host::SetGpuTextureFilter)
 *   SHARPENING      MODERN / ORIGINAL - contrast-adaptive, with the grade
 *                   (Sharpening)
 *   PRESET          ORIGINAL / MODERN (the "graphics" setting): every row
 *                   at once (GraphicsPresetValue); CUSTOM once one differs.
 *   MODERN by default (GraphicsDefault); TEXTURES MODERN in it but on an
 *   integrated GPU.
 *   LOD             ORIGINAL / HIGH - the cars' and the traffic's detailed
 *                   models out to longer distances (see the LOD distances)
 *   PARTICLES       ORIGINAL / MORE / MOST - Nehalem's patch, lifted with the
 *                   MODS' (port/tools/mods_spec.py)
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ps2host/high_fps.h"
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

/* A GRAPHICS row's value in a preset: ORIGINAL the game's own pictures,
   MODERN the PC's (lighter on an integrated GPU). */
const char* GraphicsPresetValue(const char* key, bool modern) {
    const bool original = !modern;
    const std::string k = key;
    const bool light = IntegratedGpu();
    if (k == "ao") return original || light ? "off" : "on";
    if (light && k == "shadows") return original ? "original" : "soft";
    if (light && k == "blur") return "original";
    // TEXTURES: MODERN (mipmapped textures trilinear and anisotropic: no
    // bands in motion) in the MODERN preset; not on an integrated GPU (its cost).
    if (k == "textures") return original || light ? "original" : "modern";
    // LOD HIGH (the detailed models farther) and PARTICLES MORE in MODERN.
    if (k == "lod") return original ? "original" : "high";
    if (k == "particles") return original ? "original" : "more";
    // BLOOM and MOTION BLUR LOW in MODERN: the softer glow, the shorter streaks.
    if (k == "bloom" || k == "blur") return original ? "original" : "modern_low";
    if (k == "shadows" || k == "lighting" || k == "reflections" || k == "smoke" || k == "dof" || k == "sharpen")
        return original ? "original" : "modern";
    return "original";
}

/* A GRAPHICS row's value unless the player chose one: the preset's (the
   "graphics" setting, PRESET's row - "original" pins the game's own
   pictures, as the recorded runs do; read each time: PRESET changes it). */
const char* GraphicsDefault(const char* key) {
    return GraphicsPresetValue(key, ps2host::Setting("graphics", "modern") != "original");
}

/* PARTICLES: 0 the game's, 1 MORE, 2 MOST - the "particles" setting, or
   (saved before it) the two mods' switches, else the preset's. */
int ParticlesLevel() {
    std::string v = ps2host::Setting("particles", "");
    if (v.empty()) {
        // (the switches as saved, OFF too: the player's choice then)
        const bool saved = !ps2host::Setting("mod.particles_most", "").empty() ||
                           !ps2host::Setting("mod.particles_more", "").empty();
        if (ps2host::SettingOn("mod.particles_most", false)) v = "most";
        else if (ps2host::SettingOn("mod.particles_more", false)) v = "more";
        else v = saved ? "original" : GraphicsDefault("particles");
    }
    return v == "most" ? 2 : v == "more" ? 1 : 0;
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
enum class EffectId : u32 { kTint = 0xF0, kOcclusion = 1, kMotionBlur = 2, kBloom = 3, kGrade = 4, kSceneDepth = 5,
                            kDepthOfField = 6, kReflections = 7 };

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
   body, when it is in front of the camera as a chase camera has it.
   Above 60 pictures a second the camera is blended between the game's last
   two steps (ps2host/high_fps.h) and the car drawn with it, but the race
   block's place is the last step's: blended here the same way - else the
   car's sharp sphere (the blur) and its share of AO and shadow jumped
   against the drawn car every other picture, its highlights flickering. */
struct CarTrack {
    float last[3] = {}, now[3] = {};
    bool known = false;
};
CarTrack g_car_track[2];

bool CarInView(const Camera& k, int view, float* out) {
    const u32 at = 0x0064E710u + 0x27D0u * u32(view & 1);
    const float raw[3] = {F32(at), F32(at + 4u), F32(at + 8u)};
    CarTrack& track = g_car_track[view & 1];
    if (!track.known || raw[0] != track.now[0] || raw[1] != track.now[1] || raw[2] != track.now[2]) {
        const float step[3] = {raw[0] - track.now[0], raw[1] - track.now[1], raw[2] - track.now[2]};
        const bool cut = !track.known || step[0] * step[0] + step[1] * step[1] + step[2] * step[2] > 50.0f * 50.0f;
        for (int i = 0; i < 3; ++i) {
            track.last[i] = cut ? raw[i] : track.now[i];
            track.now[i] = raw[i];
        }
        track.known = true;
    }
    const float t = ps2host::HighFpsPictureTime();
    float place[3];
    for (int i = 0; i < 3; ++i) place[i] = track.last[i] + (track.now[i] - track.last[i]) * t;
    const float car[3] = {place[0] - k.pos[0], place[1] - k.pos[1], place[2] - k.pos[2]};
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
        p[11] = 22.0f;      // the longest shadow, metres
    }
    // The player's car (p12-15: its centre, view space, and size): no sun's
    // shadow of its own on it, and less occlusion - its paint keeps its
    // gloss; what it casts on the road stays.
    if (CarInView(k, PlayerOf(v), &p[12])) p[15] = 2.6f;
    // The wider scale of occlusion (p16 its strength, p17 its radius): the
    // broad shade at walls' feet and under awnings.
    if (p[5] > 0.0f) {
        p[16] = 0.6f;
        p[17] = 5.0f;
    }
    EmitEffect(c, EffectId::kOcclusion, v, p, 22);
}

/* REFLECTIONS SSR LOW (1) or HIGH (2), else 0 ("ssr", saved before LOW
   and HIGH, LOW). */
int SsrLevel() {
    const std::string r = Graphics("reflections");
    return r == "ssr_high" ? 2 : r == "ssr_low" || r == "ssr" ? 1 : 0;
}

/* REFLECTIONS on the cars' paint (screen-space): where the paint's ray
   finds what the picture shows - a rival beside, the road, a wall - that,
   over the sphere map's; elsewhere the sphere map as it was. After the
   occlusion, before the fog (it fogs them too). */
void Reflections(lifted::Context& c, bool high) {
    const View v = CurrentView();
    const Camera k = ViewCamera(v);
    if (!k.valid || !v.zbuf) return;
    float p[22] = {};
    DepthParams(k, p);
    p[6] = 60.0f;           // the farthest paint reflecting
    // LOW: traced at the occlusion's resolution; HIGH: at the full one
    // (sharp), farther and a little stronger.
    p[10] = high ? 40.0f : 30.0f;   // the ray's longest, metres
    p[11] = high ? 1.75f : 1.5f;    // strength (seen on the bodies' sides and lower edges, not a mirror)
    p[12] = 0.25f;                  // Fresnel's base (face on)
    p[16] = high ? 1.0f : 0.0f;     // traced at the full resolution
    p[13] = 3.0f;           // the normal's footprint, pixels at the PS2's resolution
    EmitEffect(c, EffectId::kReflections, v, p, 22);
}

/* MOTION BLUR MODERN: the camera's own motion, a frame's, the boost making
   the shutter longer. `boost` 0-1. MODERN LOW: half the shutter, streaks
   at most 3% of the height (5%) - fewer taps too (by the streak's length). */
struct Motion {
    Camera last;
    u32 picture = ~0u;
    u64 blank = 0;              // ps2host::FrameNumber() then
};
Motion g_motion[2];

bool ModernBlur() {
    const std::string blur = Graphics("blur");
    return blur == "modern" || blur == "modern_low";
}

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
    // motion scaled to the shutter: a 120th of a second, a 60th boosting.
    const float moved = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const bool low = Graphics("blur") == "modern_low";
    const double shutter = (1.0 + std::clamp(boost, 0.0f, 1.0f)) / (low ? 240.0 : 120.0);
    const float scale = float(std::min(shutter / std::max(elapsed, 1e-3), 4.0));
    // (the camera's turning at half its share: a chase camera swings, and a
    // whole picture smeared by it reads as a fault, not as speed)
    for (int i = 0; i < 12; ++i) {
        const float identity = (i % 5 == 0) ? 1.0f : 0.0f;
        const float share = (i % 4 == 3) ? scale : 0.5f * scale;
        p[4 + i] = (moved > 8.0f || turn < 2.9f) ? identity : identity + share * (p[4 + i] - identity);
    }
    p[16] = 1.0f;                                              // (a gain on it)
    p[17] = low ? 0.03f : 0.05f;                               // the most, a share of the height
    // The player's car (its place in the race's block, 0x0064E710; player
    // 2's not known: none), a sphere around it kept sharp - only where it is
    // in front of the camera as a chase camera has it.
    if (CarInView(k, view, &p[18])) p[21] = 2.6f;
    EmitEffect(c, EffectId::kMotionBlur, v, p, 22);
}

/* CINEMATIC CAMERAS MODERN: depth of field in the game's takedown cameras
   (a view window of 0.58: 60 degrees) - told from the race's chase camera
   (1.0 and wider, its field of view following the speed) and crash mode's
   aftermath, which the player steers (0.65), by their narrower window;
   focused on what the middle of the view shows (the victim). Once a view a picture, after
   the blur. */
u32 g_dofed[2] = {~0u, ~0u};

void CinematicFocus(lifted::Context& c) {
    const View v = CurrentView();
    if (!v.zbuf || Split(v) || Graphics("dof") != "modern") return;
    const int view = PlayerOf(v);
    if (g_dofed[view] == g_pictures[view]) return;
    g_dofed[view] = g_pictures[view];
    const Camera k = ViewCamera(v);
    if (!k.valid || k.window_x > 0.62f) return;
    float p[22] = {};
    DepthParams(k, p);
    p[4] = 0.7f;            // aperture
    p[5] = 0.009f;          // the largest circle, a share of the height
    p[6] = 0.0f;            // focus: the view's middle (the takedown's victim)
    EmitEffect(c, EffectId::kDepthOfField, v, p, 22);
}

/* SHARPENING MODERN: the grade's contrast-adaptive sharpening, its
   strength (0 none) - stronger at the PS2's own resolution, where a pixel
   is coarse, lighter as the internal resolution rises. */
float Sharpening() {
    if (Graphics("sharpen") != "modern") return 0.0f;
    return 0.45f;
}

/* LIGHTING MODERN's grade, the parameters 12-16 of the glow and the grade
   (17: SHARPENING). */
void GradeParams(float* p) {
    p[9] = 0.5f;            // the picture's white: its colours are halved (the frame's copy doubles them)
    p[12] = 1.0f;           // the bright end's shoulder
    p[13] = 1.07f;          // contrast
    p[14] = 1.08f;          // saturation
    p[15] = 0.15f;          // vignette
    p[16] = 0.01f;          // warmth
    p[17] = Sharpening();   // SHARPENING
}
bool ModernLighting() { return Graphics("lighting") == "modern"; }
void NeutralGrade(float* p) {
    p[9] = 0.5f;
    p[12] = 0.0f;
    p[13] = 1.0f;
    p[14] = 1.0f;
    p[15] = 0.0f;
    p[16] = 0.0f;
    p[17] = Sharpening();
}

/* BLOOM MODERN: the glow of the PC's, from the track's threshold and
   intensity (bloom a0: +0, +4), at the internal resolution. MODERN LOW: from
   brighter lights, at half the strength. */
void ModernBloom(lifted::Context& c, bool low) {
    const View v = CurrentView();
    const u32 bloom = u32(c.r[4]);
    const float threshold = F32(bloom), intensity = F32(bloom + 4u);
    float p[22] = {};
    // (in the picture's white: the track's threshold, 0.75 of the halved
    // colours' range, is 1.5 times white)
    // (the threshold a little above the track's and the strength under it:
    // the lights and the sky glow, not a veil over the whole sunlit scene)
    p[0] = std::clamp(2.0f * threshold - 0.75f, 0.5f, 1.7f);
    p[1] = 0.45f * std::clamp(intensity, 0.0f, 2.0f);
    p[2] = 0.35f;
    if (low) {
        p[0] = std::min(p[0] + 0.15f, 1.75f);
        p[1] *= 0.5f;
    }
    if (ModernLighting()) GradeParams(p);
    else NeutralGrade(p);
    EmitEffect(c, EffectId::kBloom, v, p, 22);
}

void Grade(lifted::Context& c) {
    const View v = CurrentView();
    float p[22] = {};
    if (ModernLighting()) GradeParams(p);
    else NeutralGrade(p);
    EmitEffect(c, EffectId::kGrade, v, p, 22);
}

/* CB3Bloom::Render(bloom, hold, reset): a0 the view's bloom (+0x50 of the
   view's effects, P1 0x00666090, P2 0x00666130), set each frame by
   0x0030A9D0 from the track's tweaks - threshold +0 (0.75: the bright pass
   takes what is above 0xBF), intensity +4 (1.0: the glow's grey as it is
   added back) and persistence +8 (0.5, up to 0.9 boosting: the share of the
   frame before kept, the glow's trails). BLOOM MODERN HIGH and LOW draw the
   PC's glow from them instead. */
void BloomRender(lifted::Context& c) {
    const std::string strength = Graphics("bloom");
    // (the glow's switch: OFF draws none, and no grade of the glow's)
    if ((strength == "modern" || strength == "modern_low") && generated::ReadU8(kBloomOn)) {
        ModernBloom(c, strength == "modern_low");
        return;
    }
    F_002659C0(c);
    if (ModernLighting() || Sharpening() > 0.0f) Grade(c);
}

/* 0x002FA9A0: the radial blur, the race's (the blur byte gates it there)
   and the frontend's radial-blur objects' (0x003D2FD0, which the byte does
   not): MOTION BLUR OFF skips it for those too. */
void RadialBlur(lifted::Context& c) {
    const std::string blur = Graphics("blur");
    if (blur == "off") return;
    // MODERN in the race (the frontend's radial blurs have no 3D view of
    // their own: the game's, there).
    if (ModernBlur() && CurrentView().zbuf && generated::ReadU8(kBlurOn)) {
        CameraBlur(c, F32(u32(c.r[4]) + 0x1Cu));
        CinematicFocus(c);
        return;
    }
    F_002FA9A0(c);
    CinematicFocus(c);
}

/* 0x00426C10: the camera's directional blur (the race's, instead of the
   radial one when the camera asks): MODERN's blur in its place. */
void DirectionalBlur(lifted::Context& c) {
    if (ModernBlur() && CurrentView().zbuf) {
        CameraBlur(c, 0.0f);
        CinematicFocus(c);
        return;
    }
    F_00426C10(c);
    CinematicFocus(c);
}

/* 0x002419C0: the fog, AO before it. */
bool g_split = false;            // the views drawn last split (Split)

void Fog(lifted::Context& c) {
    ++g_pictures[PlayerOf(CurrentView())];
    g_split = Split(CurrentView());
    if (Graphics("ao") == "on" || Graphics("shadows") == "modern") Occlusion(c);
    if (SsrLevel() > 0) Reflections(c, SsrLevel() == 2);
    // SMOKE AND FIRE MODERN: the scene's depth saved for the particles drawn
    // after the view's effects (soft particles).
    if (Graphics("smoke") == "modern") {
        const View v = CurrentView();
        const Camera k = ViewCamera(v);
        if (k.valid && v.zbuf) {
            float p[22] = {};
            DepthParams(k, p);
            EmitEffect(c, EffectId::kSceneDepth, v, p, 22);
        }
    }
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
    // BLOOM's old HIGH and LOW (the game's glow scaled; and BLOOM STRENGTH's
    // before them, a playtest build's) are gone: ORIGINAL - once, the
    // settings all read.
    static bool migrated = false;
    if (!migrated) {
        migrated = true;
        if (!ps2host::Setting("bloom_strength", "").empty()) ps2host::SetSetting("bloom_strength", "");
        const std::string bloom = ps2host::Setting("bloom", "");
        if (bloom == "high" || bloom == "low") ps2host::SetSetting("bloom", "original");
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
    SetDetail(Graphics("lod") == "high");
    // REFLECTIONS MODERN: the cars' environment map - its three faces (FBP
    // 0x18B, 0x193, 0x19B, their Z 0x16B-0x17B) and the sphere map warped
    // from them (FBP 0x16B, 256 wide: TBP 0x2D60 as the paint samples it) -
    // at the internal resolution, and the paint's reflection by a Fresnel
    // term (ps2host::SetGpuDrawTag).
    // (in a split screen only with the blur and the glow MODERN: the game's
    // own blurs there read buffers in the same pages at the PS2's size - a
    // washed-out player 2)
    const bool split_ok = ModernBlur() &&
                          (Graphics("bloom") == "modern" || Graphics("bloom") == "modern_low");
    const std::string reflections = Graphics("reflections");
    const bool modern_reflections = (reflections == "modern" || SsrLevel() > 0) && (!g_split || split_ok);
    ps2host::SetGpuScaleRule(modern_reflections ? 0x16Bu : 0u, modern_reflections ? 0x1A7u : 0u, modern_reflections ? 4u : 0u);
    // (tag bit 3: the paint marked in the stencil, for the effects' screen-space reflections)
    const bool ssr = modern_reflections && SsrLevel() > 0;
    ps2host::SetGpuDrawTag(0x2D60u, modern_reflections ? (ssr ? 9u : 1u) : 0u);
    // TEXTURE FILTERING: MODERN, mipmapped textures filtered as a modern
    // GPU does - their detail at the internal resolution, no bands, the road
    // sharp; or the player's filter, forced on the 3D.
    const std::string textures = Graphics("textures");
    ps2host::SetGpuModernTextures(textures == "modern");
    if (textures == "nearest") ps2host::SetGpuTextureFilter(1);
    else if (textures == "bilinear") ps2host::SetGpuTextureFilter(2);
    else if (textures == "trilinear") ps2host::SetGpuTextureFilter(3);
    else if (textures == "aniso4") ps2host::SetGpuTextureFilter(4, 4);
    else if (textures == "aniso8") ps2host::SetGpuTextureFilter(4, 8);
    else if (textures == "aniso16") ps2host::SetGpuTextureFilter(4, 16);
    else ps2host::SetGpuTextureFilter(0);
    // SMOKE AND FIRE MODERN: the race's particle atlases (two upload slots,
    // a 256x256 4-bit texture each) soft where they meet the scene.
    static const u32 kParticles[] = {0x3B20u, 0x35E0u};
    ps2host::SetGpuSoftParticles(kParticles, Graphics("smoke") == "modern" ? 2 : 0);
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
    ps2host::Override(0x002659C0u, BloomRender, "the glow: BLOOM MODERN HIGH and LOW (PC SETTINGS)");
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
