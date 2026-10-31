/*
 * The button prompts as the player plays: the buttons the player's controls
 * (controls.cpp) put where the game asks for its own - the DualShock's,
 * an Xbox controller's or the keyboard's keys, whichever was used last, and
 * the mouse's buttons for choosing and going back while the mouse is. A key
 * or a click switches, as a controller's button does back.
 *
 * The prompts are the frontend's button textures (the legend along the
 * bottom of its screens, and wherever else the game draws them): the port's
 * own art takes their place as a texture pack's would (texture_pack.h's
 * overrides), drawn in the same places in them, so the game draws keys where
 * it drew buttons. The art is drawn here, as the bindings change - each
 * button slot the game's art has (Cross, Start, L1, ...) showing what the
 * menus' binding for it is: a key's cap, a PlayStation mark, an Xbox letter.
 * Every mark is drawn from shapes and strokes (the engine's prompt_art.h):
 * outlines in the game's own manner, coloured as its are - and the game's
 * own art kept where nothing differs from it. The GPU renderer's alone - the
 * software GS draws the game's.
 *
 * PS2HOST_PROMPTS=keyboard or mouse shows those from the start (a scripted
 * run's); PS2HOST_PROMPTS_DUMP=<folder> writes each texture drawn there as a
 * BMP, to look at.
 */


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "controls.h"
#include "ps2host/host.h"
#include "ps2host/prompt_art.h"
#include "ps2host/run.h"
#include "ps2host/texture_pack.h"

namespace {

namespace pack = ps2host::texture_pack;
using controls::Device;
using controls::Style;
using namespace ps2host::prompt_art;

/* What one of the game's button slots shows: the device's binding for it. */
struct Look {
    Device device = Device::kController;
    Style style = Style::kPlayStation;
    bool mouse = false;             // the mouse's buttons for Cross and Triangle
};

ps2host::bindings::Style ArtStyle(const Look& look) {
    return look.style == Style::kXbox ? ps2host::bindings::Style::kXbox : ps2host::bindings::Style::kPlayStation;
}

void Slot(Canvas& c, const Box& cell, const Look& look, int input, Tint tint, float stroke) {
    if (look.mouse && (input == kCross || input == kTriangle)) {
        MouseMark(c, cell, input == kCross ? 0 : 1, TintColour(tint == Tint::kOwn ? Tint::kWhite : tint));
        return;
    }
    const int bound = controls::MenuBinding(look.device, input);
    if (look.device == Device::kKeyboard) {
        if (bound > 0) KeyCap(c, cell, bound, tint == Tint::kOwn ? Tint::kWhite : tint, stroke);
        return;
    }
    if (bound >= 0) Button(c, cell, bound, ArtStyle(look), tint, stroke);
}

// ------------------------------------------------------------------ the textures

/* The textures (as texture pack files name them), their size, and the boxes
   of the marks in them (x0, y0, x1, y1, in the game's texels) - the places
   the game's own art has them (taken with PS2HOST_TEXTURE_DUMP). */
constexpr char kAtlasName[] = "6c432b9f02f261c7-1c2b765d5e998877-000019d3";     // 128 x 64: face buttons, Start, Select
constexpr char kArmsName[] = "e2cabf8d5022260a-e76ee08bb6750c0f-00001154";      // 32 x 16: the D-pad's arms
constexpr char kShoulderLName[] = "a0753932ac46bf20-758febaacdf3a257-00001593"; // 64 x 32: L1, L2
constexpr char kShoulderRName[] = "6c0ca9b9090026ff-c9b41b58a3740b4-00001593";  // 64 x 32: R1, R2
constexpr char kImpactName[] = "adbaa8e56489a4b-99f532662aa4f3aa-00001593";     // 64 x 32: the race's R1 (Impact Time)

constexpr Box kCrossCell{2, 3, 13, 14}, kTriangleCell{16, 2, 27, 12}, kCircleCell{1, 16, 14, 29},
              kSquareCell{16, 17, 27, 28};
constexpr Box kRing{1, 33, 22, 54};                    // the disc every face button sits in
constexpr Box kStartCells[] = {{30, 1, 47, 15}, {85, 40, 108, 57}};
constexpr Box kStartDarkCells[] = {{49, 4, 60, 12}, {86, 3, 105, 16}};
constexpr Box kSelectCell{30, 18, 47, 30}, kSelectDarkCell{49, 20, 62, 28};
constexpr Box kStartWord{67, 21, 125, 37};
constexpr Box kArmUp{2, 1, 14, 15}, kArmLeft{16, 2, 30, 14};     // drawn as the down and right arms
constexpr Box kShoulderCells[] = {{1, 4, 31, 28}, {33, 4, 63, 28}};
constexpr Box kImpactCell{5, 4, 59, 28};

/* The Start word (the title's PRESS START): the binding's name. */
std::string StartWord(const Look& look) {
    const int bound = controls::MenuBinding(look.device, kStart);
    if (look.device == Device::kKeyboard) return controls::BoundName(Device::kKeyboard, controls::Context::kMenus, kStart);
    if (bound == kStart) return "START";
    return controls::BoundName(Device::kController, controls::Context::kMenus, kStart);
}

Canvas Atlas(const Look& look) {
    Canvas c(128, 64);
    if (look.device == Device::kKeyboard) {
        // A key's outline round each face button's.
        const Box k = kRing.Square(1.0f);
        c.RoundedOutline(k.x0 + 1.0f, k.y0 + 1.0f, k.x1 - 1.0f, k.y1 - 1.0f, (k.W() - 2.0f) * 0.28f, 1.8f, kWhite);
    } else {
        c.Ring(kRing.Cx(), kRing.Cy(), kRing.W() / 2 - 1.0f, 1.5f, kWhite);
    }
    const float stroke = look.device == Device::kKeyboard ? 1.3f : 1.15f;
    Slot(c, kCrossCell, look, kCross, Tint::kOwn, stroke);
    Slot(c, kTriangleCell, look, kTriangle, Tint::kOwn, stroke);
    Slot(c, kCircleCell, look, kCircle, Tint::kOwn, stroke);
    Slot(c, kSquareCell, look, kSquare, Tint::kOwn, stroke);
    for (const Box& cell : kStartCells) Slot(c, cell, look, kStart, Tint::kWhite, 1.5f);
    for (const Box& cell : kStartDarkCells) Slot(c, cell, look, kStart, Tint::kDark, 2.0f);
    Slot(c, kSelectCell, look, kSelect, Tint::kWhite, 1.5f);
    Slot(c, kSelectDarkCell, look, kSelect, Tint::kDark, 2.0f);
    Text(c, StartWord(look), kStartWord.x0, kStartWord.y0, kStartWord.x1, kStartWord.y1, kGrey, 0.85f, 1.7f);
    return c;
}

/* The D-pad's arms as arrow keys, a key each with its arrow pointing out
   from the middle. The game's arms point in - the first piece is the down
   arm (flipped for up), the second the right (for left) - so the arrows
   here point down and right. */
Canvas Arms() {
    Canvas c(32, 16);
    for (const auto& [cell, way] : {std::pair<Box, int>{kArmUp, 2}, std::pair<Box, int>{kArmLeft, 1}}) {
        const Box k = cell.Square(1.0f);
        c.RoundedOutline(k.x0 + 0.6f, k.y0 + 0.6f, k.x1 - 0.6f, k.y1 - 0.6f, (k.W() - 1.2f) * 0.25f, 1.4f, kWhite);
        ArrowMark(c, cell, way, kWhite);
    }
    return c;
}

/* L1/L2 or R1/R2 as the game's grey keys, the binding on each. */
Canvas Shoulders(const Look& look, int first, int second) {
    Canvas c(64, 32);
    const int inputs[2] = {first, second};
    for (int i = 0; i < 2; ++i) {
        const Box& b = kShoulderCells[i];
        c.RoundedBox(b.x0 + 0.5f, b.y0 + 0.5f, b.x1 - 0.5f, b.y1 - 0.5f, 5.0f, kKeyEdge);
        c.RoundedBox(b.x0 + 2.0f, b.y0 + 2.0f, b.x1 - 2.0f, b.y1 - 2.0f, 4.0f, kKeyFace);
        const Box inside{b.x0 + 3.0f, b.y0 + 3.0f, b.x1 - 3.0f, b.y1 - 3.0f};
        const int bound = controls::MenuBinding(look.device, inputs[i]);
        if (look.device == Device::kKeyboard) {
            if (bound > 0) KeyCap(c, inside, bound, Tint::kKey, 2.4f);
        } else if (bound >= 0) {
            const bool face = bound == kCross || bound == kCircle || bound == kSquare || bound == kTriangle;
            if (face) Button(c, inside.Square(0.8f), bound, ArtStyle(look), Tint::kOwn, 2.0f);
            else Button(c, inside, bound, ArtStyle(look), Tint::kKey, 2.4f);
        }
    }
    return c;
}

/* The race's own R1 key (a crash's IMPACT TIME), as its grey key: the
   racing binding for boost. */
Canvas ImpactKey(const Look& look) {
    Canvas c(64, 32);
    const Box& b = kImpactCell;
    c.RoundedBox(b.x0, b.y0, b.x1, b.y1, 6.0f, kKeyEdge);
    c.RoundedBox(b.x0 + 1.5f, b.y0 + 1.5f, b.x1 - 1.5f, b.y1 - 1.5f, 5.0f, kKeyFace);
    const Box inside{b.x0 + 3.0f, b.y0 + 3.0f, b.x1 - 3.0f, b.y1 - 3.0f};
    const int bound = controls::RacingBinding(look.device, kR1);
    if (look.device == Device::kKeyboard) {
        if (bound > 0) KeyCap(c, inside, bound, Tint::kKey, 2.6f);
    } else if (bound >= 0) {
        const bool face = bound == kCross || bound == kCircle || bound == kSquare || bound == kTriangle;
        if (face) Button(c, inside.Square(0.85f), bound, ArtStyle(look), Tint::kOwn, 2.0f);
        else Button(c, inside, bound, ArtStyle(look), Tint::kKey, 2.6f);
    }
    return c;
}

// ------------------------------------------------------------------ showing them

enum class Prompts { kController, kKeyboard, kMouse };

Prompts g_prompts = Prompts::kController;
u64 g_input_seen = 0;           // LastInputDevice's frame, last time
std::string g_shown;            // what the textures show now (Signature)

/* PS2HOST_PROMPTS_DUMP=<folder>: a texture drawn, as a BMP there. */
void Dump(const char* name, const std::vector<u32>& pixels, int w, int h) {
    static const char* folder = std::getenv("PS2HOST_PROMPTS_DUMP");
    if (!folder) return;
    static int serial = 0;
    char path[512];
    std::snprintf(path, sizeof path, "%s/%03d_%s.bmp", folder, serial++, name);
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    const u32 bytes = u32(w) * u32(h) * 4;
    unsigned char header[54] = {'B', 'M'};
    auto put = [&](int at, u32 v) { std::memcpy(header + at, &v, 4); };
    put(2, 54 + bytes);
    put(10, 54);
    put(14, 40);
    put(18, u32(w));
    put(22, u32(-h));               // top down
    header[26] = 1;
    header[28] = 32;
    put(34, bytes);
    std::fwrite(header, 1, sizeof header, f);
    for (u32 p : pixels) {
        const unsigned char bgra[4] = {u8(p >> 16), u8(p >> 8), u8(p), u8(p >> 24)};
        std::fwrite(bgra, 1, 4, f);
    }
    std::fclose(f);
}

/* The images drawn, kept by what they show (drawing one takes a moment). */
std::shared_ptr<const pack::Image> Image(const std::string& key, const char* name, Canvas (*draw)(const Look&),
                                         const Look& look) {
    static std::map<std::string, std::shared_ptr<const pack::Image>> made;
    auto found = made.find(key);
    if (found != made.end()) return found->second;
    const Canvas c = draw(look);
    std::vector<u32> pixels = c.Pixels();
    Dump(name, pixels, c.Width(), c.Height());
    std::shared_ptr<const pack::Image> image = pack::MakeImage(std::move(pixels), u32(c.Width()), u32(c.Height()));
    if (made.size() > 32) made.clear();
    made[key] = image;
    return image;
}

/* What the prompts look like now, as a key: the device, the style and the bindings. */
std::string Signature(const Look& look) {
    std::string key = look.device == Device::kKeyboard ? (look.mouse ? "mouse" : "keyboard")
                      : look.style == Style::kXbox      ? "xbox"
                                                        : "playstation";
    for (int i = 0; i < 16; ++i) key += "," + std::to_string(controls::MenuBinding(look.device, i));
    return key + ";" + std::to_string(controls::RacingBinding(look.device, kR1));
}

void Set(const char* name, std::shared_ptr<const pack::Image> image) {
    if (!pack::SetOverride(name, std::move(image))) std::fprintf(stderr, "[prompts] could not use the art for %s\n", name);
}

void Show(Prompts prompts) {
    Look look;
    look.device = prompts == Prompts::kController ? Device::kController : Device::kKeyboard;
    look.mouse = prompts == Prompts::kMouse;
    look.style = controls::PromptStyle();
    const std::string signature = Signature(look);
    if (signature == g_shown) return;
    g_shown = signature;
    // The race's Impact Time key: the game's own for a DualShock's R1.
    if (look.device == Device::kController && look.style == Style::kPlayStation &&
        controls::RacingBinding(Device::kController, kR1) == kR1)
        Set(kImpactName, nullptr);
    else
        Set(kImpactName, Image(signature + "/impact", "impact", ImpactKey, look));
    // A DualShock's buttons as the game's: its own art.
    if (look.device == Device::kController && look.style == Style::kPlayStation &&
        controls::MenuBindingsAsGame(Device::kController)) {
        for (const char* name : {kAtlasName, kArmsName, kShoulderLName, kShoulderRName}) Set(name, nullptr);
        std::fprintf(stderr, "[prompts] the controller's (the game's own)\n");
        return;
    }
    Set(kAtlasName, Image(signature + "/atlas", "atlas", Atlas, look));
    Set(kShoulderLName, Image(signature + "/l", "shoulders_l", [](const Look& l) { return Shoulders(l, kL1, kL2); }, look));
    Set(kShoulderRName, Image(signature + "/r", "shoulders_r", [](const Look& l) { return Shoulders(l, kR1, kR2); }, look));
    // The D-pad: arrow keys for the keyboard; a controller's own.
    if (look.device == Device::kKeyboard) Set(kArmsName, Image("arms", "arms", [](const Look&) { return Arms(); }, look));
    else Set(kArmsName, nullptr);
    std::fprintf(stderr, "[prompts] %s\n", prompts == Prompts::kMouse      ? "the mouse's"
                                           : prompts == Prompts::kKeyboard ? "the keyboard's"
                                           : look.style == Style::kXbox    ? "the controller's (Xbox)"
                                                                           : "the controller's (as bound)");
}

}  // namespace

/* Whether the controller's prompts show (the controller used last). */
bool ControllerPrompts() { return g_prompts == Prompts::kController; }

/* Once a pad read (mouse.cpp's MouseTick): `mouse_used`, the mouse moved,
   clicked or turned since the last. */
void UpdatePrompts(bool mouse_used) {
    // PS2HOST_PROMPTS=keyboard or mouse: those from the start (to look at
    // them in a scripted run), until something is used.
    static bool started = false;
    if (!started) {
        started = true;
        if (const char* forced = std::getenv("PS2HOST_PROMPTS")) {
            const std::string want = forced;
            if (want == "keyboard" || want == "mouse") g_prompts = want == "keyboard" ? Prompts::kKeyboard : Prompts::kMouse;
        }
    }
    u64 frame = 0;
    const ps2host::InputDevice device = ps2host::LastInputDevice(&frame);
    // (A phone's taps come as the mouse's clicks: its prompts stay the
    // controller's - the on-screen one's buttons.)
    if (mouse_used && !ps2host::OnPhone()) g_prompts = Prompts::kMouse;
    else if (frame != g_input_seen)
        g_prompts = device == ps2host::InputDevice::kKeyboard ? Prompts::kKeyboard : Prompts::kController;
    g_input_seen = frame;
    // Drawn again when the device, the style or a binding changes (Show
    // compares what they would look like with what they do).
    static unsigned generation = 0;
    static Prompts shown = Prompts::kController;
    static Style style = Style::kPlayStation;
    static bool first = true;
    const Style now_style = controls::PromptStyle();
    if (first || g_prompts != shown || now_style != style || controls::Generation() != generation) {
        first = false;
        shown = g_prompts;
        style = now_style;
        generation = controls::Generation();
        Show(g_prompts);
    }
}
