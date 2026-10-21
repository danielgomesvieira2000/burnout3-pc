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
 *   BLOOM           ORIGINAL / HIGH / OFF / LOW - HIGH and LOW the glow's
 *                   threshold, intensity and trails changed (see BloomRender)
 *   MOTION BLUR     ORIGINAL / OFF (the key "blur", as it was saved)
 *   SHADOWS         ORIGINAL / SOFT / OFF - SOFT the blob drawn here (see
 *                   SoftBlob)
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
    const u32 frame = generated::ReadU32(camera + 0x04u);
    if (frame) {
        for (int i = 0; i < 3; ++i) {
            k.right[i] = F32(frame + 0x50u + 4u * i);
            k.up[i] = F32(frame + 0x60u + 4u * i);
            k.at[i] = F32(frame + 0x70u + 4u * i);
            k.pos[i] = F32(frame + 0x80u + 4u * i);
        }
    }
    k.valid = k.window_x > 0.0f && k.window_y > 0.0f;
    return k;
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

/* AO (ambient occlusion), before the fog: the corners, the cars' contact
   with the road, the walls' feet. */
void Occlusion(lifted::Context& c) {
    const View v = CurrentView();
    const Camera k = ReadCamera(g_cameras[v.index & 1]);
    if (!k.valid || !v.zbuf) return;
    float p[22] = {};
    DepthParams(k, p);
    p[4] = 1.5f;            // radius, world units
    p[5] = 0.9f;            // strength
    p[6] = 180.0f;          // gone by this distance
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
    const int view = int(v.index & 1);
    if (g_blurred[view] == g_pictures[view]) return;    // (the other blur sent it)
    g_blurred[view] = g_pictures[view];
    const Camera k = ReadCamera(g_cameras[view]);
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
    // motion scaled to the shutter: an 80th of a second, a 40th boosting.
    const float moved = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const double shutter = (1.0 + std::clamp(boost, 0.0f, 1.0f)) / 80.0;
    const float scale = float(std::min(shutter / std::max(elapsed, 1e-3), 4.0));
    for (int i = 0; i < 12; ++i) {
        const float identity = (i % 5 == 0) ? 1.0f : 0.0f;
        p[4 + i] = (moved > 8.0f || turn < 2.9f) ? identity : identity + scale * (p[4 + i] - identity);
    }
    p[16] = 1.0f;                                              // (a gain on it)
    p[17] = 0.07f;                                             // the most, a share of the height
    p[18] = 0.0f;                                              // the car: centre, radius, far side
    p[19] = 0.42f;
    p[20] = 0.42f;
    p[21] = 9.0f;
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
bool ModernLighting() { return Is("lighting", "original", "modern"); }
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
    p[0] = std::clamp(2.0f * threshold - 0.8f, 0.5f, 1.6f);
    p[1] = 0.4f * std::clamp(intensity, 0.0f, 2.0f);
    p[2] = 0.3f;
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
    const std::string strength = ps2host::Setting("bloom", "original");
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
    const std::string blur = ps2host::Setting("blur", "original");
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
    if (Is("blur", "original", "modern") && CurrentView().zbuf) {
        CameraBlur(c, 0.0f);
        return;
    }
    F_00426C10(c);
}

/* 0x002419C0: the fog, AO before it. */
void Fog(lifted::Context& c) {
    ++g_pictures[CurrentView().index & 1];
    if (Is("ao", "off", "on")) Occlusion(c);
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
    g_bloom.Set(Is("bloom", "original", "off"));
    g_blur.Set(Is("blur", "original", "off"));
    const std::string shadows = ps2host::Setting("shadows", "original");
    g_shadows.Set(shadows == "off");
    if (shadows != g_shadows_now) {
        static std::shared_ptr<const pack::Image> soft;
        if (shadows == "soft") {
            if (!soft) soft = SoftBlob();
            pack::SetOverride(kBlobName, soft);
        } else if (g_shadows_now == "soft") {
            pack::SetOverride(kBlobName, std::shared_ptr<const pack::Image>());
        }
        g_shadows_now = shadows;
    }
    SetDetail(Is("lod", "original", "high"));
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
    }
}
