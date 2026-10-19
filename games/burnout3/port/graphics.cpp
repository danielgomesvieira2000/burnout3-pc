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
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "ps2host/texture_pack.h"

// The glow, the radial blur (see BloomRender, RadialBlur).
void F_002659C0(lifted::Context& c);
void F_002FA9A0(lifted::Context& c);

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

/* CB3Bloom::Render(bloom, hold, reset): a0 the view's bloom (+0x50 of the
   view's effects, P1 0x00666090, P2 0x00666130), set each frame by
   0x0030A9D0 from the track's tweaks - threshold +0 (0.75: the bright pass
   takes what is above 0xBF), intensity +4 (1.0: the glow's grey as it is
   added back) and persistence +8 (0.5, up to 0.9 boosting: the share of the
   frame before kept, the glow's trails). BLOOM HIGH and LOW scale them for
   this draw, the game's own values back after it. */
void BloomRender(lifted::Context& c) {
    const std::string strength = ps2host::Setting("bloom", "original");
    if (strength != "low" && strength != "high") {
        F_002659C0(c);
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
}

/* 0x002FA9A0: the radial blur, the race's (the blur byte gates it there)
   and the frontend's radial-blur objects' (0x003D2FD0, which the byte does
   not): MOTION BLUR OFF skips it for those too. */
void RadialBlur(lifted::Context& c) {
    if (Is("blur", "original", "off")) return;
    F_002FA9A0(c);
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
}
