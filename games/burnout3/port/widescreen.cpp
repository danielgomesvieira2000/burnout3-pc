/*
 * Burnout 3 in a wider picture (ASPECT RATIO, PC SETTINGS).
 *
 * The game draws for a 4:3 television. Its 3D is made wider at the camera
 * (SetViewWindow); its 2D - the HUD, the frontend's words and sprites, the
 * labels over the rivals - keeps its 4:3 shape, each element brought in
 * towards the middle of the screen, or towards its left or right edge (an
 * anchor). Anchors are settings ("hud.<context>.<object>.<cell>" = left,
 * right or centre), set with the HUD editor (F10) and saved as they change;
 * kDefaultAnchors gives the port's own.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// RenderWare's RwCameraSetViewWindow (see SetViewWindow).
void F_001E2110(lifted::Context& c);
// The 2D batch: its builders (each appends GS packets to a scratchpad
// buffer), its flush (see Flush2D); the frontend's sprites (see Sprites2D).
void F_002178E0(lifted::Context& c);
void F_00217A20(lifted::Context& c);
void F_00217C40(lifted::Context& c);
void F_00217D40(lifted::Context& c);
void F_00217E70(lifted::Context& c);
void F_00217FC0(lifted::Context& c);
void F_00218400(lifted::Context& c);
void F_002187B0(lifted::Context& c);
void F_00218D00(lifted::Context& c);
void F_00219150(lifted::Context& c);
void F_00219530(lifted::Context& c);
void F_00219830(lifted::Context& c);
void F_00219A20(lifted::Context& c);
void F_00219CA0(lifted::Context& c);
void F_00219D50(lifted::Context& c);
void F_00219F20(lifted::Context& c);
void F_0021A4F0(lifted::Context& c);
void F_00364920(lifted::Context& c);
// The race's world each frame: the cars, the traffic, the clock (see RaceWorld).
void F_0019A550(lifted::Context& c);

namespace {

// ------------------------------------------------------------ the camera
//
// Its cameras (0x00211CC0 sets one's field of view) give RenderWare a view
// window of tan(fov / 2) across and that over 4/3 up, each frame (the field
// of view follows the speed). For a wider picture a 4:3 window is made
// wider by as much - the same view up and down, more at the sides - and the
// display shows the picture that shape. RenderWare culls to the window it
// is given.

/* 0x001E2110(camera, &window): RenderWare's RwCameraSetViewWindow. */
void SetViewWindow(lifted::Context& c) {
    const float wider = ps2host::PictureAspect() / (4.0f / 3.0f);
    const u32 window = u32(c.r[5]);
    const u32 x_bits = generated::ReadU32(window), y_bits = generated::ReadU32(window + 4);
    float x, y;
    std::memcpy(&x, &x_bits, 4);
    std::memcpy(&y, &y_bits, 4);
    // A 4:3 window alone (a camera drawing for the screen), the game's own
    // copy put back after.
    if (std::fabs(wider - 1.0f) < 0.001f || !(x > 0.0f) || std::fabs(y / x - 0.75f) > 0.002f) {
        F_001E2110(c);
        return;
    }
    const float wide = x * wider;
    u32 wide_bits;
    std::memcpy(&wide_bits, &wide, 4);
    generated::Write32(window, wide_bits);
    F_001E2110(c);
    generated::Write32(window, x_bits);
}

// ------------------------------------------------------------ elements and anchors
//
// An element is what one piece of the game's code draws in one part of the
// screen: the object (the function whose call drew it), what called that
// (its context: the race's HUD, a frontend screen), and the cell of a 3x3
// grid over the 4:3 picture its middle lies in (the race's POS and LAP are
// the same object, top left and top right). The same in every run.

enum class Anchor { kCentre, kLeft, kRight, kStretch };

struct ElementId {
    u32 context = 0, object = 0;
    char cell[3] = {'?', '?', 0};        // row T M B, column L C R
    bool operator<(const ElementId& o) const {
        // Rows top to bottom, each left to right.
        if (cell[0] != o.cell[0]) return std::string("TMB").find(cell[0]) < std::string("TMB").find(o.cell[0]);
        if (cell[1] != o.cell[1]) return std::string("LCR").find(cell[1]) < std::string("LCR").find(o.cell[1]);
        if (object != o.object) return object < o.object;
        return context < o.context;
    }
    bool operator==(const ElementId& o) const {
        return context == o.context && object == o.object && cell[0] == o.cell[0] && cell[1] == o.cell[1];
    }
    bool known() const { return object != 0; }
    std::string Key() const {
        char key[48];
        std::snprintf(key, sizeof key, "hud.%06X.%06X.%s", unsigned(context), unsigned(object), cell);
        return key;
    }
};

/* The port's own anchors (the HUD editor's settings take precedence). */
const std::pair<const char*, Anchor> kDefaultAnchors[] = {
    // The race (chosen in play with the HUD editor): position, the boost
    // bar and their panels on the left, the speed on the right.
    {"hud.1A0790.1A6290.BC", Anchor::kLeft},
    {"hud.1A0790.1A6290.BL", Anchor::kLeft},
    {"hud.1A0790.1D5FB0.TL", Anchor::kLeft},
    {"hud.1A0790.381030.BR", Anchor::kRight},
    {"hud.1A0790.381AB0.TL", Anchor::kLeft},
    {"hud.1A0790.3CD920.BR", Anchor::kRight},
    {"hud.1A0790.3CD920.TL", Anchor::kLeft},
    {"hud.1D75C0.1A7B00.BC", Anchor::kLeft},
    {"hud.1D75C0.1A7B00.BL", Anchor::kLeft},
    {"hud.1D75C0.1A8180.BL", Anchor::kLeft},
    // The frontend.
    {"hud.30D6E0.31B130.TC", Anchor::kStretch},
    {"hud.30D6E0.31B130.TL", Anchor::kLeft},
    {"hud.30D6E0.3B2EE0.MC", Anchor::kStretch},
    {"hud.30D6E0.3B2EE0.MR", Anchor::kStretch},
    {"hud.30D6E0.3D7B40.BC", Anchor::kStretch},
    {"hud.30D6E0.3D7B40.BR", Anchor::kRight},
};

const char* AnchorName(Anchor a) {
    switch (a) {
        case Anchor::kLeft: return "left";
        case Anchor::kRight: return "right";
        case Anchor::kStretch: return "stretch";
        default: return "centre";
    }
}

/* An element's anchor: its setting, else the port's default, else centred;
   remembered until the editor changes one. */
std::map<std::string, Anchor> g_anchors;
Anchor AnchorOf(const ElementId& id) {
    if (!id.known()) return Anchor::kCentre;
    const std::string key = id.Key();
    auto found = g_anchors.find(key);
    if (found != g_anchors.end()) return found->second;
    Anchor anchor = Anchor::kCentre;
    for (const auto& [name, value] : kDefaultAnchors)
        if (key == name) anchor = value;
    const std::string set = ps2host::Setting(key, "");
    if (set == "left") anchor = Anchor::kLeft;
    else if (set == "right") anchor = Anchor::kRight;
    else if (set == "centre") anchor = Anchor::kCentre;
    else if (set == "stretch") anchor = Anchor::kStretch;
    g_anchors[key] = anchor;
    return anchor;
}

/* 4:3 over the picture's shape: 1 for a 4:3 picture. */
float Squeeze() { return std::min(1.0f, (4.0f / 3.0f) / ps2host::PictureAspect()); }

/* x (about `centre`, `half` its distance to the picture's edges) brought in
   by k towards the middle, or the edge it is anchored to; stretched, as the
   game drew it - across the wider picture. */
float Place(float x, float centre, float half, float k, Anchor anchor) {
    if (anchor == Anchor::kStretch) return x;
    const float a = anchor == Anchor::kLeft ? centre - half : anchor == Anchor::kRight ? centre + half : centre;
    return a + (x - a) * k;
}

/* The cell of a point of the 4:3 picture, (0, 0) its top left, 640 x 448. */
void CellOf(float x, float y, char cell[3]) {
    cell[0] = y < 448.0f / 3 ? 'T' : y < 896.0f / 3 ? 'M' : 'B';
    cell[1] = x < 640.0f / 3 ? 'L' : x < 1280.0f / 3 ? 'C' : 'R';
    cell[2] = 0;
}

/* Who drew: the object (the caller of the function drawing) and its
   context, from the guest call stack, the drawing function innermost. The
   context is two calls further out: the frontend's layers (0x001D75C0)
   call every object, and what calls them is the race (0x001A0790) or a
   frontend screen (0x0030D6E0). */
void Caller(ElementId* id) {
    const std::vector<u32>& calls = ps2host::CurrentCalls();
    const std::size_t n = calls.size();
    id->object = n >= 2 ? calls[n - 2] : 0;
    id->context = n >= 4 ? calls[n - 4] : 0;
}

// ------------------------------------------------------------ the HUD editor
//
// F10 in a wider picture: a panel over the picture lists the elements on
// screen and their anchors; Page Up and Page Down choose one (it blinks),
// Home or numpad 4 anchors it left, Insert or numpad 5 in the middle, End or
// numpad 6 right, Delete or numpad 8 stretches it across the picture.
// P holds the race still (the world not moved on, its picture and HUD
// drawn every frame) - till P again or the editor closes - saved at once.

struct Editor {
    bool on = false;
    ElementId chosen;
    std::map<ElementId, std::chrono::steady_clock::time_point> seen;   // drawn lately
    bool keys[8] = {};
    std::size_t shown = ~std::size_t(0);   // how many the panel lists
    bool paused = false;                    // the race held still (P)
};
Editor g_editor;

/* What the race's HUD elements are (the panel names them). */
const char* ElementName(const ElementId& id) {
    switch (id.object) {
        case 0x003CD920: return "panel";
        case 0x00381AB0: return id.cell[1] == 'L' ? "position" : id.cell[1] == 'R' ? "lap" : "counter";
        case 0x00381030: return "speed";
        case 0x001A8180: return "boost bar";
        default: return "";
    }
}

const char* CellName(const char cell[3]) {
    static const char* const kRows[] = {"top", "middle", "bottom"};
    static const char* const kColumns[] = {"left", "centre", "right"};
    static char name[24];
    const std::size_t row = std::string("TMB").find(cell[0]), column = std::string("LCR").find(cell[1]);
    std::snprintf(name, sizeof name, "%s %s", row < 3 ? kRows[row] : "?", column < 3 ? kColumns[column] : "?");
    return name;
}

/* An element was drawn (the editor lists what is on screen). */
void Seen(const ElementId& id) {
    if (g_editor.on && id.known()) g_editor.seen[id] = std::chrono::steady_clock::now();
}

/* Whether it is hidden now: the chosen one blinks. */
bool Hidden(const ElementId& id) {
    if (!g_editor.on || !(id == g_editor.chosen)) return false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (ms / 250) % 2 == 1;
}

/* The panel: what is on screen, the one chosen, the keys. */
void ShowStatus() {
    if (!g_editor.on) {
        ps2host::SetWindowOverlay({});
        ps2host::SetWindowStatus("");
        return;
    }
    std::vector<std::string> lines = {g_editor.paused ? "HUD EDITOR  (F10 closes)   - PAUSED (P resumes)"
                                                      : "HUD EDITOR  (F10 closes)   P pauses the race",
                                      "PgUp/PgDn choose   Home left   Insert centre   End right   Delete stretch", ""};
    std::size_t index = 0, count = 0;
    for (const auto& [id, when] : g_editor.seen) {
        ++count;
        const bool chosen = id == g_editor.chosen;
        if (chosen) index = count;
        char text[160];
        std::snprintf(text, sizeof text, "%s %-13s %-10s %06X/%06X   %s", chosen ? ">" : " ", CellName(id.cell),
                      ElementName(id), unsigned(id.object), unsigned(id.context), AnchorName(AnchorOf(id)));
        lines.push_back(text);
    }
    if (!count) lines.push_back("  (no HUD drawn)");
    g_editor.shown = count;
    char title[96];
    std::snprintf(title, sizeof title, "HUD EDIT %zu/%zu", index, count);
    ps2host::SetWindowStatus(title);
    ps2host::SetWindowOverlay(lines);
}

/* A key's press (down now, up before). */
bool Press(int slot, int vk, int vk2 = 0) {
    const bool down = ps2host::WindowFocused() && (ps2host::HostKeyDown(vk) || (vk2 && ps2host::HostKeyDown(vk2)));
    const bool pressed = down && !g_editor.keys[slot];
    g_editor.keys[slot] = down;
    return pressed;
}

/* The editor's keys, read as the 2D is drawn. */
void EditorInput() {
    constexpr int kF10 = 0x79, kPageUp = 0x21, kPageDown = 0x22, kHome = 0x24, kEnd = 0x23, kInsert = 0x2D;
    constexpr int kPad4 = 0x64, kPad5 = 0x65, kPad6 = 0x66, kDelete = 0x2E, kPad8 = 0x68, kP = 'P';
    if (Press(0, kF10)) {
        g_editor.on = !g_editor.on && Squeeze() < 1.0f;
        g_editor.paused = false;
        g_editor.seen.clear();
        g_editor.chosen = ElementId{};
        g_editor.shown = ~std::size_t(0);
        std::fprintf(stderr, "[hud] editor %s\n", g_editor.on ? "on" : Squeeze() < 1.0f ? "off" : "off (a 4:3 picture)");
        ShowStatus();
    }
    if (!g_editor.on) return;
    // What has not been drawn for a while is off the screen; the panel
    // follows what is.
    const auto now = std::chrono::steady_clock::now();
    for (auto it = g_editor.seen.begin(); it != g_editor.seen.end();)
        it = now - it->second > std::chrono::milliseconds(500) ? g_editor.seen.erase(it) : std::next(it);
    if (g_editor.seen.size() != g_editor.shown) ShowStatus();
    std::vector<ElementId> list;
    for (const auto& [id, when] : g_editor.seen) list.push_back(id);
    const bool next = Press(1, kPageDown), previous = Press(2, kPageUp);
    if ((next || previous) && !list.empty()) {
        std::size_t at = list.size();
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == g_editor.chosen) at = i;
        if (at == list.size()) at = next ? list.size() - 1 : 0;
        g_editor.chosen = list[(at + (next ? 1 : list.size() - 1)) % list.size()];
        ShowStatus();
    }
    Anchor set = Anchor::kCentre;
    bool setting = false;
    if (Press(3, kHome, kPad4)) setting = true, set = Anchor::kLeft;
    if (Press(4, kInsert, kPad5)) setting = true, set = Anchor::kCentre;
    if (Press(5, kEnd, kPad6)) setting = true, set = Anchor::kRight;
    if (Press(6, kDelete, kPad8)) setting = true, set = Anchor::kStretch;
    if (Press(7, kP)) {
        g_editor.paused = !g_editor.paused;
        std::fprintf(stderr, "[hud] race %s\n", g_editor.paused ? "paused" : "resumed");
        ShowStatus();
    }
    if (setting && g_editor.chosen.known()) {
        const std::string key = g_editor.chosen.Key();
        ps2host::SetSetting(key, AnchorName(set));
        g_anchors[key] = set;
        std::fprintf(stderr, "[hud] %s anchored %s\n", key.c_str(), AnchorName(set));
        ShowStatus();
    }
}

// ------------------------------------------------------------ the 2D batch
//
// 0x0021A4F0 flushes a batch of finished GS packets that its builders (the
// HUD: 0x00219830, 0x00218D00, 0x002187B0 and the like) appended in
// scratchpad, [gp-0x6D20] to [gp-0x5D98]. Each builder's call is a piece:
// the bytes it appended and who called it. At the flush every XYZ word is
// found (per primitive, PRIM tracked through the batch) and brought in -
// where it draws into the 640-wide picture (FRAME tracked; not the 64x32
// target the race's sky reads), by its element's anchor. A primitive across
// the whole picture keeps its vertices at the edges (backdrops, fades; a
// highlight's rule still reaches them, its ends brought in); a scissor
// narrower than the picture comes in too (the frontend's tickers).

/* What the batches' A+D writes leave set, from one flush to the next. */
struct Batch2D {
    u32 fbw = 10;                       // FRAME_1's FBW: 10 the 640-wide picture
    int centre = 0x8000;                // XYOFFSET_1's X + 320 pixels, in GS units (1/16)
    int middle = 0x7200 + 224 * 16;     // ...and its Y + 224
    u32 prim = 6;                       // PRIM's type: 6 a sprite
    u32 starts = 0;                     // PRIM writes so far: each begins a strip or fan afresh
};
Batch2D g_batch;

struct Piece {
    u32 begin, end;
    ElementId id;                       // its cell found at the flush
};
std::vector<Piece> g_pieces;            // appended since the last flush

/* A builder: its call is a piece. A flush inside it leaves it none. */
template <void (*Build)(lifted::Context&)>
void Builder(lifted::Context& c) {
    const u32 gp = u32(c.r[28]);
    const u32 begin = generated::ReadU32(gp - 0x5D98u);
    ElementId id;
    Caller(&id);
    Build(c);
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    if (end > begin) g_pieces.push_back({begin, end, id});
}

bool IsXyz(u32 reg) { return reg == 0x4 || reg == 0x5 || reg == 0xC || reg == 0xD; }
u64 Read64(u32 at) { return generated::ReadU32(at) | u64(generated::ReadU32(at + 4)) << 32; }

/* An XYZ word: the primitive it is a vertex of, and where it draws - into
   the picture or not (another target), about which middle. */
struct Vertex2D {
    u32 at, prim, start;
    bool picture;
    int centre, middle;
};

/* SCISSOR_1 (an A+D write at `at`) narrower than the picture: brought in
   with what it clips, or that shows past its panel. The whole picture's
   stays. */
void SqueezeScissor(u32 at, float k) {
    const u64 scissor = Read64(at);
    const int x0 = int(scissor & 0x7FF), x1 = int((scissor >> 16) & 0x7FF);
    if (x0 <= 8 && x1 >= 631) return;
    const auto in = [k](int x) { return std::clamp(int(320.0f + float(x - 320) * k + 0.5f), 0, 2047); };
    const u64 squeezed = (scissor & ~0x07FF07FFull) | u64(in(x0)) | u64(in(x1)) << 16;
    generated::Write32(at, u32(squeezed));
}

/* One GIF tag's vertices (PACKED: its XYZ registers and A+D's; REGLIST)
   with their primitives, its A+D writes applied to g_batch (and a narrow
   scissor squeezed); where the next tag begins. */
u32 TagVertices(u32 at, u32 end, std::vector<Vertex2D>* vertices, float k) {
    const u64 tag = Read64(at), regs = Read64(at + 8);
    at += 16;
    const u32 nloop = u32(tag & 0x7FFF), flg = u32(tag >> 58) & 3;
    const auto set_prim = [](u32 value) {
        g_batch.prim = value & 7;
        ++g_batch.starts;
    };
    if ((tag >> 46) & 1) set_prim(u32(tag >> 47));                // PRE: the tag's PRIM
    u32 nreg = u32(tag >> 60) & 0xF;
    if (!nreg) nreg = 16;
    if (flg == 0) {
        for (u32 l = 0; l < nloop && at + 16 <= end; ++l)
            for (u32 r = 0; r < nreg && at + 16 <= end; ++r, at += 16) {
                const u32 reg = u32(regs >> (4 * r)) & 0xF;
                if (IsXyz(reg)) {
                    vertices->push_back({at, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
                } else if (reg == 0x0) {
                    set_prim(generated::ReadU32(at));
                } else if (reg == 0xE) {
                    const u32 address = generated::ReadU8(at + 8);
                    if (IsXyz(address)) {
                        vertices->push_back({at, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
                    } else if (address == 0x00) {
                        set_prim(generated::ReadU32(at));
                    } else if (address == 0x4C) {
                        g_batch.fbw = u32(Read64(at) >> 16) & 0x3F;
                    } else if (address == 0x18) {
                        g_batch.centre = int(Read64(at) & 0xFFFF) + 320 * 16;
                        g_batch.middle = int((Read64(at) >> 32) & 0xFFFF) + 224 * 16;
                    } else if (address == 0x40 && k < 1.0f && g_batch.fbw == 10) {
                        SqueezeScissor(at, k);
                    }
                }
            }
        return at;
    }
    if (flg == 1) {
        const u32 words = nloop * nreg;
        for (u32 i = 0; i < words; ++i) {
            const u32 reg = u32(regs >> (4 * (i % nreg))) & 0xF;
            if (IsXyz(reg)) vertices->push_back({at + 8 * i, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
            else if (reg == 0x0) set_prim(generated::ReadU32(at + 8 * i));
        }
        return at + (words + 1) / 2 * 16;
    }
    return at + nloop * 16;
}

/* Vertices per primitive of a type: a point, a line, a triangle, a sprite;
   0 for a strip or fan (all of its run, one shape). */
u32 PrimitiveVertices(u32 prim) {
    switch (prim) {
        case 0: return 1;
        case 1: case 6: return 2;
        case 3: return 3;
        default: return 0;
    }
}

/* A batch's vertices into the picture, each placed by its piece's element. */
void PlaceVertices(const std::vector<Vertex2D>& vertices, float k) {
    const int half = 312 * 16;
    // Each vertex's piece (pieces are in order, as appended), and each
    // piece's element: the cell of the middle of what it drew.
    std::vector<int> owner(vertices.size(), -1);
    std::vector<float> lo_x(g_pieces.size(), 1e9f), hi_x(g_pieces.size(), -1e9f);
    std::vector<float> lo_y(g_pieces.size(), 1e9f), hi_y(g_pieces.size(), -1e9f);
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        const u32 at = vertices[v].at;
        auto it = std::upper_bound(g_pieces.begin(), g_pieces.end(), at,
                                   [](u32 a, const Piece& p) { return a < p.begin; });
        if (it == g_pieces.begin()) continue;
        --it;
        if (at >= it->end) continue;
        const int p = int(it - g_pieces.begin());
        owner[v] = p;
        const u32 word = generated::ReadU32(at);
        const float x = float(int(word & 0xFFFF) - vertices[v].centre) / 16.0f + 320.0f;
        const float y = float(int(word >> 16) - vertices[v].middle) / 16.0f + 224.0f;
        lo_x[p] = std::min(lo_x[p], x);
        hi_x[p] = std::max(hi_x[p], x);
        lo_y[p] = std::min(lo_y[p], y);
        hi_y[p] = std::max(hi_y[p], y);
    }
    std::vector<Anchor> anchors(g_pieces.size(), Anchor::kCentre);
    std::vector<bool> hidden(g_pieces.size(), false);
    for (std::size_t p = 0; p < g_pieces.size(); ++p) {
        if (lo_x[p] > hi_x[p]) continue;
        CellOf((lo_x[p] + hi_x[p]) / 2, (lo_y[p] + hi_y[p]) / 2, g_pieces[p].id.cell);
        anchors[p] = AnchorOf(g_pieces[p].id);
        hidden[p] = Hidden(g_pieces[p].id);
        Seen(g_pieces[p].id);
    }
    for (std::size_t i = 0; i < vertices.size();) {
        const u32 size = PrimitiveVertices(vertices[i].prim);
        std::size_t j = i + 1;
        if (size == 0) {
            while (j < vertices.size() && vertices[j].start == vertices[i].start) ++j;
        } else {
            j = std::min(vertices.size(), i + size);
        }
        int x0 = 0x10000, x1 = -1;
        for (std::size_t v = i; v < j; ++v) {
            const int x = int(generated::ReadU32(vertices[v].at) & 0xFFFF);
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
        }
        // One across the picture keeps its vertices at the edges there.
        const int centre = vertices[i].centre;
        const bool across = x0 <= centre - half && x1 >= centre + half;
        const int p = owner[i];
        const Anchor anchor = p >= 0 ? anchors[p] : Anchor::kCentre;
        const bool hide = p >= 0 && hidden[p];
        for (std::size_t v = i; v < j; ++v) {
            const u32 word = generated::ReadU32(vertices[v].at);
            if (hide) {
                // The chosen element, blinking: every vertex at the first's.
                generated::Write32(vertices[v].at, generated::ReadU32(vertices[i].at));
                continue;
            }
            const int x = int(word & 0xFFFF);
            if (across && (x <= centre - half || x >= centre + half)) continue;
            const float placed = Place(float(x), float(centre), 320.0f * 16, k, anchor);
            const int at_x = int(std::lround(placed));
            generated::Write32(vertices[v].at, (word & ~0xFFFFu) | (u32(at_x) & 0xFFFFu));
        }
        i = j;
    }
}

/* 0x0021A4F0: the batch placed, then flushed. */
void Flush2D(lifted::Context& c) {
    EditorInput();
    const u32 gp = u32(c.r[28]);
    u32 at = generated::ReadU32(gp - 0x6D20u);
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    const float k = Squeeze();
    std::vector<Vertex2D> all;
    while (at + 16 <= end && end - at < 0x10000) at = TagVertices(at, end, &all, k);
    if (k < 1.0f) {
        std::vector<Vertex2D> picture;
        for (const Vertex2D& v : all)
            if (v.picture) picture.push_back(v);
        PlaceVertices(picture, k);
    }
    g_pieces.clear();
    F_0021A4F0(c);
}

// ------------------------------------------------------------ the frontend's sprites
//
// 0x00364920(vertices, count) draws the frontend objects' sprites (and the
// rivals' labels, which the game places with its own 4:3 projection): 32-byte
// vertices, x and y floats about the middle of the screen. Placed for the
// draw, the objects' own put back after (they are kept from frame to frame).

void Sprites2D(lifted::Context& c) {
    const float k = Squeeze();
    const u32 vertices = u32(c.r[4]), count = u32(c.r[5]);
    if (k >= 1.0f || count == 0 || count > 4096) {
        F_00364920(c);
        return;
    }
    std::vector<u32> saved(2 * count);
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (u32 i = 0; i < count; ++i) {
        saved[2 * i] = generated::ReadU32(vertices + 32 * i);
        saved[2 * i + 1] = generated::ReadU32(vertices + 32 * i + 4);
        float x, y;
        std::memcpy(&x, &saved[2 * i], 4);
        std::memcpy(&y, &saved[2 * i + 1], 4);
        lo_x = std::min(lo_x, x);
        hi_x = std::max(hi_x, x);
        lo_y = std::min(lo_y, y);
        hi_y = std::max(hi_y, y);
    }
    ElementId id;
    Caller(&id);
    CellOf((lo_x + hi_x) / 2 + 320.0f, (lo_y + hi_y) / 2 + 224.0f, id.cell);
    const Anchor anchor = AnchorOf(id);
    const bool hide = Hidden(id);
    Seen(id);
    for (u32 i = 0; i < count; ++i) {
        float x;
        std::memcpy(&x, &saved[2 * i], 4);
        x = hide ? 0.0f : Place(x, 0.0f, 320.0f, k, anchor);
        u32 bits;
        std::memcpy(&bits, &x, 4);
        generated::Write32(vertices + 32 * i, bits);
        if (hide) generated::Write32(vertices + 32 * i + 4, saved[1]);
    }
    F_00364920(c);
    for (u32 i = 0; i < count; ++i) {
        generated::Write32(vertices + 32 * i, saved[2 * i]);
        generated::Write32(vertices + 32 * i + 4, saved[2 * i + 1]);
    }
}

/* 0x0019A550, the race's world each frame (the cars, the traffic, the
   clock): not run while the HUD editor holds the race still. Its picture
   is drawn apart (0x00134A20), so the race stands, drawn - the HUD's
   placing seen as it changes. */
unsigned long long g_race_world_calls = 0;

void RaceWorld(lifted::Context& c) {
    ++g_race_world_calls;
    if (g_editor.on && g_editor.paused) return;
    F_0019A550(c);
}

}  // namespace

/* Where a frontend element's x is in the picture (640 wide, as the 4:3 one):
   brought in towards the middle in a wider one, or to the edge it is
   anchored to - the element by its object (the function drawing it, a
   frontend object's +0x3C) on a frontend screen (0x0030D6E0) and the cell
   its middle (cx, cy, in the 640 x 448 picture) is in, as Flush2D and
   Sprites2D place it. For the mouse (mouse.cpp). */
float FrontendPlace(u32 object, float x, float cx, float cy) {
    ElementId id;
    id.context = 0x0030D6E0u;
    id.object = object;
    CellOf(cx, cy, id.cell);
    return Place(x, 320.0f, 320.0f, Squeeze(), AnchorOf(id));
}

/* How many times the race's world has been updated (the mouse: no pointer in
   a race). */
unsigned long long RaceWorldCalls() { return g_race_world_calls; }

void RegisterWidescreen() {
    ps2host::Override(0x0019A550u, RaceWorld, "the race's world each frame: held still by the HUD editor (P)");
    ps2host::Override(0x001E2110u, SetViewWindow, "RwCameraSetViewWindow: wider for ASPECT RATIO (PC SETTINGS)");
    ps2host::Override(0x0021A4F0u, Flush2D, "a 2D batch's flush: the HUD placed in a wider picture");
    ps2host::Override(0x00364920u, Sprites2D, "the frontend's sprites: placed in a wider picture");
    ps2host::Override(0x002178E0u, Builder<F_002178E0>, "a 2D builder: its piece");
    ps2host::Override(0x00217A20u, Builder<F_00217A20>, "a 2D builder: its piece");
    ps2host::Override(0x00217C40u, Builder<F_00217C40>, "a 2D builder: its piece");
    ps2host::Override(0x00217D40u, Builder<F_00217D40>, "a 2D builder: its piece");
    ps2host::Override(0x00217E70u, Builder<F_00217E70>, "a 2D builder: its piece");
    ps2host::Override(0x00217FC0u, Builder<F_00217FC0>, "a 2D builder: its piece");
    ps2host::Override(0x00218400u, Builder<F_00218400>, "a 2D builder: its piece");
    ps2host::Override(0x002187B0u, Builder<F_002187B0>, "a 2D builder: its piece");
    ps2host::Override(0x00218D00u, Builder<F_00218D00>, "a 2D builder: its piece");
    ps2host::Override(0x00219150u, Builder<F_00219150>, "a 2D builder: its piece");
    ps2host::Override(0x00219530u, Builder<F_00219530>, "a 2D builder: its piece");
    ps2host::Override(0x00219830u, Builder<F_00219830>, "a 2D builder: its piece");
    ps2host::Override(0x00219A20u, Builder<F_00219A20>, "a 2D builder: its piece");
    ps2host::Override(0x00219CA0u, Builder<F_00219CA0>, "a 2D builder: its piece");
    ps2host::Override(0x00219D50u, Builder<F_00219D50>, "a 2D builder: its piece");
    ps2host::Override(0x00219F20u, Builder<F_00219F20>, "a 2D builder: its piece");
}
