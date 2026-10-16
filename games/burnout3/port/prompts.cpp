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
 * Every mark is drawn from shapes and strokes (no font, no image of
 * anyone's): outlines in the game's own manner, coloured as its are - and
 * the game's own art kept where nothing differs from it. The GPU renderer's
 * alone - the software GS draws the game's.
 *
 * PS2HOST_PROMPTS=keyboard or mouse shows those from the start (a scripted
 * run's); PS2HOST_PROMPTS_DUMP=<folder> writes each texture drawn there as a
 * BMP, to look at.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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
#include "ps2host/run.h"
#include "ps2host/texture_pack.h"

namespace {

namespace pack = ps2host::texture_pack;
using controls::Device;
using controls::Style;

// ------------------------------------------------------------------ drawing

constexpr int kScale = 8;           // the art, this many times the game's texture

struct Point {
    float x, y;
};
using Line = std::vector<Point>;

struct Colour {
    float r, g, b;
};
constexpr Colour kWhite{255, 255, 255};
constexpr Colour kGrey{180, 180, 180};          // the game's START word
constexpr Colour kDark{68, 69, 69};             // the game's dark Start and Select
constexpr Colour kKeyEdge{28, 28, 28}, kKeyFace{150, 150, 150}, kKeyText{250, 250, 250};
// The DualShock's marks, as the game's art has them.
constexpr Colour kCrossBlue{169, 197, 255}, kTriangleTeal{107, 201, 204}, kCirclePink{255, 141, 166},
                 kSquareMagenta{250, 164, 251};
// An Xbox controller's letters, in its colours made as light as the game's marks.
constexpr Colour kXboxA{120, 210, 100}, kXboxB{255, 120, 120}, kXboxX{110, 170, 255}, kXboxY{245, 215, 90};

float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float SegmentDistance(float px, float py, Point a, Point b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = dx * dx + dy * dy;
    const float t = length > 0.0f ? Clamp01(((px - a.x) * dx + (py - a.y) * dy) / length) : 0.0f;
    const float ex = px - (a.x + t * dx), ey = py - (a.y + t * dy);
    return std::sqrt(ex * ex + ey * ey);
}

/* A rounded rectangle's signed distance (inside negative). */
float RoundRect(float px, float py, float x0, float y0, float x1, float y1, float radius) {
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    const float qx = std::abs(px - cx) - ((x1 - x0) / 2 - radius), qy = std::abs(py - cy) - ((y1 - y0) / 2 - radius);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

/* A convex polygon's signed distance (near enough at its edges, where it counts). */
float Convex(float px, float py, const std::vector<Point>& points) {
    float area = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point a = points[i], b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    const float side = area > 0.0f ? 1.0f : -1.0f;
    float d = -1e9f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point a = points[i], b = points[(i + 1) % points.size()];
        const float ex = b.x - a.x, ey = b.y - a.y, length = std::sqrt(ex * ex + ey * ey);
        if (length <= 0.0f) continue;
        // Outward: to the right of an edge going round anticlockwise (y down).
        d = std::max(d, side * ((px - a.x) * ey - (py - a.y) * ex) / length);
    }
    return d;
}

/* A texture's art, drawn kScale times larger than the game's texels; in
   texels throughout. Colour premultiplied by coverage. */
class Canvas {
public:
    Canvas(int width, int height) : w_(width * kScale), h_(height * kScale), px_(std::size_t(w_) * h_ * 4, 0.0f) {}

    /* Paints `colour` where `distance` (texels, inside negative) says, within
       the box (x0, y0)-(x1, y1), anti-aliased over a pixel. */
    template <class Distance>
    void Paint(float x0, float y0, float x1, float y1, Colour colour, Distance distance) {
        const int a = std::max(0, int(std::floor(x0 * kScale)) - 1), b = std::min(w_, int(std::ceil(x1 * kScale)) + 1);
        const int c = std::max(0, int(std::floor(y0 * kScale)) - 1), d = std::min(h_, int(std::ceil(y1 * kScale)) + 1);
        for (int y = c; y < d; ++y)
            for (int x = a; x < b; ++x) {
                const float tx = (float(x) + 0.5f) / kScale, ty = (float(y) + 0.5f) / kScale;
                const float cover = Clamp01(0.5f - distance(tx, ty) * kScale);
                if (cover <= 0.0f) continue;
                float* p = &px_[(std::size_t(y) * w_ + x) * 4];
                p[0] = colour.r * cover + p[0] * (1.0f - cover);
                p[1] = colour.g * cover + p[1] * (1.0f - cover);
                p[2] = colour.b * cover + p[2] * (1.0f - cover);
                p[3] = cover + p[3] * (1.0f - cover);
            }
    }

    /* Lines `width` texels thick, round at their ends and joins: one shape
       (where they cross is no darker). */
    void Strokes(const std::vector<Line>& lines, float width, Colour colour) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const Line& line : lines)
            for (const Point& p : line) {
                x0 = std::min(x0, p.x), y0 = std::min(y0, p.y);
                x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
            }
        const float r = width / 2;
        Paint(x0 - r, y0 - r, x1 + r, y1 + r, colour, [&](float x, float y) {
            float d = 1e9f;
            for (const Line& line : lines) {
                if (line.size() == 1) d = std::min(d, SegmentDistance(x, y, line[0], line[0]));
                for (std::size_t i = 1; i < line.size(); ++i) d = std::min(d, SegmentDistance(x, y, line[i - 1], line[i]));
            }
            return d - r;
        });
    }

    void Polygon(const std::vector<Point>& points, Colour colour) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const Point& p : points) {
            x0 = std::min(x0, p.x), y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
        }
        Paint(x0, y0, x1, y1, colour, [&](float x, float y) { return Convex(x, y, points); });
    }

    void RoundedBox(float x0, float y0, float x1, float y1, float radius, Colour colour) {
        Paint(x0, y0, x1, y1, colour, [&](float x, float y) { return RoundRect(x, y, x0, y0, x1, y1, radius); });
    }

    /* A rounded rectangle's outline, `width` thick inside its edge. */
    void RoundedOutline(float x0, float y0, float x1, float y1, float radius, float width, Colour colour) {
        const float h = width / 2;
        Paint(x0, y0, x1, y1, colour, [&](float x, float y) {
            return std::abs(RoundRect(x, y, x0 + h, y0 + h, x1 - h, y1 - h, std::max(0.0f, radius - h))) - h;
        });
    }

    void Ring(float cx, float cy, float radius, float width, Colour colour) {
        Paint(cx - radius - width, cy - radius - width, cx + radius + width, cy + radius + width, colour,
              [&](float x, float y) { return std::abs(std::hypot(x - cx, y - cy) - radius) - width / 2; });
    }

    template <class Distance>
    void Shape(float x0, float y0, float x1, float y1, Colour colour, Distance distance) {
        Paint(x0, y0, x1, y1, colour, distance);
    }

    /* The art as the texture pack takes it: RGBA8, alpha as the GS's (0x80 opaque). */
    std::vector<u32> Pixels() const {
        std::vector<u32> out(std::size_t(w_) * h_);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float* p = &px_[i * 4];
            const float a = p[3];
            if (a <= 0.0f) continue;
            auto channel = [&](float v) { return u32(std::lround(std::min(255.0f, v / a))); };
            const u32 alpha = u32(std::lround(a * 128.0f));
            out[i] = channel(p[0]) | channel(p[1]) << 8 | channel(p[2]) << 16 | alpha << 24;
        }
        return out;
    }
    int Width() const { return w_; }
    int Height() const { return h_; }

private:
    int w_, h_;
    std::vector<float> px_;
};

// ------------------------------------------------------------------ the letters

/* Points along an ellipse's arc (angles in degrees, y down: 90 is the bottom). */
Line Arc(float cx, float cy, float rx, float ry, float a0, float a1, int steps = 24) {
    Line out;
    for (int i = 0; i <= steps; ++i) {
        const float a = (a0 + (a1 - a0) * float(i) / float(steps)) * 3.14159265f / 180.0f;
        out.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
    return out;
}

Line Join(Line a, const Line& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

struct Glyph {
    std::vector<Line> lines;        // in a unit box, x right, y down
    float width = 1.0f;             // against its height
};

/* The strokes of a letter, digit or mark the keys' caps use. */
Glyph GlyphOf(char ch) {
    switch (ch) {
        case 'A': return {{{{0, 1}, {0.5f, 0}, {1, 1}}, {{0.22f, 0.62f}, {0.78f, 0.62f}}}};
        case 'B':
            return {{Join(Join({{0, 1}, {0, 0}, {0.52f, 0}}, Arc(0.52f, 0.25f, 0.4f, 0.25f, -90, 90)), {{0, 0.5f}}),
                     Join(Join({{0, 0.5f}, {0.56f, 0.5f}}, Arc(0.56f, 0.75f, 0.44f, 0.25f, -90, 90)), {{0, 1}})},
                    0.8f};
        case 'C': return {{Arc(0.55f, 0.5f, 0.5f, 0.5f, -50, -310)}, 0.85f};
        case 'D': return {{Join(Join({{0, 0}, {0, 1}, {0.4f, 1}}, Arc(0.4f, 0.5f, 0.6f, 0.5f, 90, -90)), {{0, 0}})}, 0.9f};
        case 'E': return {{{{1, 0}, {0, 0}, {0, 1}, {1, 1}}, {{0, 0.5f}, {0.8f, 0.5f}}}, 0.75f};
        case 'F': return {{{{1, 0}, {0, 0}, {0, 1}}, {{0, 0.5f}, {0.8f, 0.5f}}}, 0.72f};
        case 'G': return {{Join(Arc(0.5f, 0.5f, 0.5f, 0.5f, -40, -360), {{0.55f, 0.5f}})}, 0.9f};
        case 'H': return {{{{0, 0}, {0, 1}}, {{1, 0}, {1, 1}}, {{0, 0.5f}, {1, 0.5f}}}, 0.85f};
        case 'I': return {{{{0.5f, 0}, {0.5f, 1}}, {{0.1f, 0}, {0.9f, 0}}, {{0.1f, 1}, {0.9f, 1}}}, 0.5f};
        case 'J': return {{Join({{0.85f, 0}, {0.85f, 0.68f}}, Arc(0.45f, 0.68f, 0.4f, 0.32f, 0, 165))}, 0.75f};
        case 'K': return {{{{0, 0}, {0, 1}}, {{0.95f, 0}, {0, 0.62f}}, {{0.32f, 0.4f}, {1, 1}}}, 0.82f};
        case 'L': return {{{{0, 0}, {0, 1}, {0.9f, 1}}}, 0.7f};
        case 'M': return {{{{0, 1}, {0, 0}, {0.5f, 0.65f}, {1, 0}, {1, 1}}}, 1.05f};
        case 'N': return {{{{0, 1}, {0, 0}, {1, 1}, {1, 0}}}, 0.88f};
        case 'O': return {{Arc(0.5f, 0.5f, 0.5f, 0.5f, 0, 360, 36)}, 0.95f};
        case 'P': return {{Join(Join({{0, 1}, {0, 0}, {0.56f, 0}}, Arc(0.56f, 0.27f, 0.44f, 0.27f, -90, 90)), {{0, 0.54f}})}, 0.8f};
        case 'Q': return {{Arc(0.5f, 0.48f, 0.5f, 0.48f, 0, 360, 36), {{0.6f, 0.65f}, {1.0f, 1.05f}}}, 0.95f};
        case 'R':
            return {{Join(Join({{0, 1}, {0, 0}, {0.6f, 0}}, Arc(0.6f, 0.25f, 0.38f, 0.25f, -90, 90)), {{0, 0.5f}}),
                     {{0.5f, 0.5f}, {1, 1}}},
                    0.82f};
        case 'S': return {{Join(Arc(0.5f, 0.25f, 0.45f, 0.25f, -20, -270), Arc(0.5f, 0.75f, 0.48f, 0.25f, -90, 160))}, 0.8f};
        case 'T': return {{{{0, 0}, {1, 0}}, {{0.5f, 0}, {0.5f, 1}}}, 0.85f};
        case 'U': return {{Join(Join({{0, 0}, {0, 0.62f}}, Arc(0.5f, 0.62f, 0.5f, 0.38f, 180, 0)), {{1, 0}})}, 0.85f};
        case 'V': return {{{{0, 0}, {0.5f, 1}, {1, 0}}}};
        case 'W': return {{{{0, 0}, {0.25f, 1}, {0.5f, 0.3f}, {0.75f, 1}, {1, 0}}}, 1.25f};
        case 'X': return {{{{0, 0}, {1, 1}}, {{1, 0}, {0, 1}}}};
        case 'Y': return {{{{0, 0}, {0.5f, 0.5f}, {1, 0}}, {{0.5f, 0.5f}, {0.5f, 1}}}};
        case 'Z': return {{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}};
        case '0': return {{Arc(0.5f, 0.5f, 0.5f, 0.5f, 0, 360, 36)}, 0.75f};
        case '1': return {{{{0.15f, 0.25f}, {0.6f, 0}, {0.6f, 1}}}, 0.55f};
        case '2': return {{Join(Arc(0.5f, 0.28f, 0.45f, 0.28f, -165, 25), {{0, 1}, {1, 1}})}, 0.8f};
        case '3': return {{Join(Arc(0.45f, 0.27f, 0.45f, 0.27f, -160, 90), Arc(0.45f, 0.75f, 0.47f, 0.25f, -90, 160))}, 0.8f};
        case '4': return {{{{0.72f, 1}, {0.72f, 0}, {0, 0.68f}, {1, 0.68f}}}, 0.85f};
        case '5': return {{Join({{0.92f, 0}, {0.15f, 0}, {0.07f, 0.5f}}, Arc(0.48f, 0.67f, 0.47f, 0.33f, -150, 150))}, 0.8f};
        case '6': return {{Arc(0.5f, 0.68f, 0.45f, 0.68f, -60, -180), Arc(0.5f, 0.68f, 0.45f, 0.32f, 0, 360, 30)}, 0.8f};
        case '7': return {{{{0, 0}, {1, 0}, {0.35f, 1}}}, 0.8f};
        case '8': return {{Arc(0.5f, 0.26f, 0.4f, 0.26f, 0, 360, 30), Arc(0.5f, 0.74f, 0.47f, 0.26f, 0, 360, 30)}, 0.8f};
        case '9': return {{Arc(0.5f, 0.32f, 0.45f, 0.32f, 0, 360, 30), Arc(0.5f, 0.32f, 0.45f, 0.68f, 0, 120)}, 0.8f};
        case ';': return {{{{0.5f, 0.3f}}, {{0.5f, 0.8f}, {0.35f, 1.05f}}}, 0.35f};
        case ',': return {{{{0.55f, 0.8f}, {0.35f, 1.05f}}}, 0.35f};
        case '.': return {{{{0.5f, 0.92f}}}, 0.3f};
        case '=': return {{{{0, 0.33f}, {1, 0.33f}}, {{0, 0.67f}, {1, 0.67f}}}, 0.8f};
        case '-': return {{{{0.1f, 0.5f}, {0.9f, 0.5f}}}, 0.7f};
        case '+': return {{{{0, 0.5f}, {1, 0.5f}}, {{0.5f, 0.12f}, {0.5f, 0.88f}}}, 0.8f};
        case '*':
            return {{{{0.5f, 0.15f}, {0.5f, 0.85f}}, {{0.2f, 0.32f}, {0.8f, 0.68f}}, {{0.2f, 0.68f}, {0.8f, 0.32f}}}, 0.8f};
        case '/': return {{{{0.9f, 0}, {0.1f, 1}}}, 0.6f};
        case '\\': return {{{{0.1f, 0}, {0.9f, 1}}}, 0.6f};
        case '[': return {{{{0.8f, 0}, {0.3f, 0}, {0.3f, 1}, {0.8f, 1}}}, 0.5f};
        case ']': return {{{{0.2f, 0}, {0.7f, 0}, {0.7f, 1}, {0.2f, 1}}}, 0.5f};
        case '`': return {{{{0.3f, 0}, {0.65f, 0.3f}}}, 0.5f};
        case '\'': return {{{{0.5f, 0}, {0.5f, 0.3f}}}, 0.3f};
        default: return {{}, 0.5f};     // a space
    }
}

/* Words centred in the box (x0, y0)-(x1, y1), `part` of its height (smaller
   if they would not fit across it), strokes `stroke` thick (0: by their size). */
void Text(Canvas& c, const std::string& word, float x0, float y0, float x1, float y1, Colour colour, float part,
          float stroke = 0.0f) {
    if (word.empty()) return;
    float h = (y1 - y0) * part, gap = h * 0.22f;
    std::vector<Glyph> glyphs;
    float total = 0.0f;
    for (char ch : word) {
        glyphs.push_back(GlyphOf(ch));
        total += h * 0.78f * glyphs.back().width;
    }
    total += gap * float(word.size() - 1);
    float scale = 1.0f;
    if (total > (x1 - x0) * 0.9f) scale = (x1 - x0) * 0.9f / total;
    h *= scale, gap *= scale, total *= scale;
    const float width = stroke > 0.0f ? stroke * std::min(1.0f, scale * 1.4f) : std::max(0.45f, h * 0.17f);
    float x = (x0 + x1) / 2 - total / 2;
    const float y = (y0 + y1) / 2 - h / 2;
    for (const Glyph& g : glyphs) {
        const float w = h * 0.78f * g.width;
        std::vector<Line> lines;
        for (const Line& line : g.lines) {
            Line placed;
            for (const Point& p : line)
                placed.push_back({x + width / 2 + p.x * (w - width), y + width / 2 + p.y * (h - width)});
            lines.push_back(placed);
        }
        if (!lines.empty()) c.Strokes(lines, width, colour);
        x += w + gap;
    }
}

// ------------------------------------------------------------------ the marks

struct Box {
    float x0, y0, x1, y1;
    float W() const { return x1 - x0; }
    float H() const { return y1 - y0; }
    float Cx() const { return (x0 + x1) / 2; }
    float Cy() const { return (y0 + y1) / 2; }
    /* The largest square in the middle, `part` of its side. */
    Box Square(float part = 1.0f) const {
        const float s = std::min(W(), H()) * part;
        return {Cx() - s / 2, Cy() - s / 2, Cx() + s / 2, Cy() + s / 2};
    }
};

Point At(const Box& b, float u, float v) { return {b.x0 + u * b.W(), b.y0 + v * b.H()}; }

std::vector<Point> Placed(const Box& b, std::initializer_list<Point> unit) {
    std::vector<Point> out;
    for (const Point& p : unit) out.push_back(At(b, p.x, p.y));
    return out;
}

/* The Enter key's bent arrow. */
void EnterMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.8f);
    c.Strokes({Placed(b, {{0.85f, 0.1f}, {0.85f, 0.6f}, {0.3f, 0.6f}})}, width, colour);
    c.Polygon(Placed(b, {{0.0f, 0.6f}, {0.35f, 0.3f}, {0.35f, 0.9f}}), colour);
}

/* The Backspace key's arrow, pointing left. */
void BackspaceMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const float s = std::min(cell.W(), cell.H()) * 0.85f;
    const Box b{cell.Cx() - s * 0.7f, cell.Cy() - s / 2, cell.Cx() + s * 0.7f, cell.Cy() + s / 2};
    c.Strokes({Placed(b, {{1.0f, 0.5f}, {0.3f, 0.5f}})}, width, colour);
    c.Polygon(Placed(b, {{0.0f, 0.5f}, {0.35f, 0.15f}, {0.35f, 0.85f}}), colour);
}

/* An arrow key's arrow: a triangle pointing `way` (0 up, 1 right, 2 down, 3 left). */
void ArrowMark(Canvas& c, const Box& cell, int way, Colour colour) {
    const Box b = cell.Square(0.62f);
    static const Point kUp[3] = {{0.5f, 0.1f}, {0.95f, 0.85f}, {0.05f, 0.85f}};
    std::vector<Point> points;
    for (const Point& p : kUp) {
        float u = p.x, v = p.y;
        for (int i = 0; i < way; ++i) {
            const float t = u;
            u = 1.0f - v;
            v = t;
        }
        points.push_back(At(b, u, v));
    }
    c.Polygon(points, colour);
}

/* The space bar's mark: a wide bracket opening up. */
void SpaceMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const float w = std::min(cell.W() * 0.86f, cell.H() * 1.1f), h = w * 0.32f;
    const Box b{cell.Cx() - w / 2, cell.Cy() - h / 2, cell.Cx() + w / 2, cell.Cy() + h / 2};
    c.Strokes({Placed(b, {{0, 0}, {0, 1}, {1, 1}, {1, 0}})}, width, colour);
}

/* Shift's mark: a hollow arrow pointing up. */
void ShiftMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.78f);
    c.Strokes({Placed(b, {{0.5f, 0.02f}, {0.98f, 0.5f}, {0.72f, 0.5f}, {0.72f, 0.98f}, {0.28f, 0.98f}, {0.28f, 0.5f},
                          {0.02f, 0.5f}, {0.5f, 0.02f}})},
              width, colour);
}

/* A mouse, its left (0) or right (1) button filled. */
void MouseMark(Canvas& c, const Box& cell, int button, Colour colour) {
    const float h = cell.H() * 0.86f, w = h * 0.66f;
    const float left = cell.Cx() - w / 2, top = cell.Cy() - h / 2;
    const float stroke = std::max(0.8f, h * 0.09f), split = top + h * 0.45f;
    const float x0 = left + stroke / 2, x1 = left + w - stroke / 2, y0 = top + stroke / 2, y1 = top + h - stroke / 2;
    c.RoundedOutline(left, top, left + w, top + h, w / 2, stroke, colour);
    c.Strokes({{{x0, split}, {x1, split}}, {{cell.Cx(), y0}, {cell.Cx(), split}}}, stroke, colour);
    // The button pressed, filled: the body's top on its side, a little in from the lines.
    const float gap = stroke * 1.15f;
    const float sx0 = button == 0 ? left : cell.Cx() + gap - stroke / 2, sx1 = button == 0 ? cell.Cx() - gap + stroke / 2 : left + w;
    c.Shape(left, top, left + w, split, colour, [&](float x, float y) {
        const float body = RoundRect(x, y, left + gap, top + gap, left + w - gap, top + h - gap, w / 2 - gap);
        const float side = RoundRect(x, y, sx0, top, sx1, split - gap + stroke / 2, 0.0f);
        return std::max(body, side);
    });
}

// The DualShock's marks: outlines in a box (stroke `width`).
void CrossMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.8f);
    c.Strokes({Placed(b, {{0, 0}, {1, 1}}), Placed(b, {{1, 0}, {0, 1}})}, width, colour);
}
void TriangleMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.86f);
    c.Strokes({Placed(b, {{0.5f, 0.06f}, {0.98f, 0.9f}, {0.02f, 0.9f}, {0.5f, 0.06f}})}, width, colour);
}
void CircleMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.84f);
    c.Ring(b.Cx(), b.Cy(), b.W() / 2 - width / 2, width, colour);
}
void SquareMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.8f);
    c.RoundedOutline(b.x0, b.y0, b.x1, b.y1, 0.0f, width, colour);
}
/* Start: a triangle pointing right (filled, for the dark ones). */
void StartMark(Canvas& c, const Box& cell, Colour colour, float width, bool filled) {
    const Box b = cell.Square(0.86f);
    const float h = b.H() * 0.84f;
    const Box t{b.Cx() - h * 0.45f, b.Cy() - h / 2, b.Cx() + h * 0.55f, b.Cy() + h / 2};
    if (filled) c.Polygon(Placed(t, {{0, 0}, {1, 0.5f}, {0, 1}}), colour);
    else c.Strokes({Placed(t, {{0, 0}, {1, 0.5f}, {0, 1}, {0, 0}})}, width, colour);
}
/* Select: a rectangle on its side. */
void SelectMark(Canvas& c, const Box& cell, Colour colour, float width, bool filled) {
    const float w = std::min(cell.W() * 0.86f, cell.H() * 1.2f), h = w * 0.62f;
    const Box b{cell.Cx() - w / 2, cell.Cy() - h / 2, cell.Cx() + w / 2, cell.Cy() + h / 2};
    if (filled) c.RoundedBox(b.x0, b.y0, b.x1, b.y1, 0.0f, colour);
    else c.RoundedOutline(b.x0, b.y0, b.x1, b.y1, 0.0f, width, colour);
}
/* An Xbox controller's Menu (three lines) and View (two windows) buttons. */
void MenuMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.66f);
    c.Strokes({Placed(b, {{0.1f, 0.2f}, {0.9f, 0.2f}}), Placed(b, {{0.1f, 0.5f}, {0.9f, 0.5f}}),
               Placed(b, {{0.1f, 0.8f}, {0.9f, 0.8f}})},
              width, colour);
}
void ViewMark(Canvas& c, const Box& cell, Colour colour, float width) {
    const Box b = cell.Square(0.74f);
    c.RoundedOutline(b.x0, b.y0, b.x0 + b.W() * 0.66f, b.y0 + b.H() * 0.66f, 0.0f, width, colour);
    c.Strokes({Placed(b, {{0.46f, 0.66f}, {0.46f, 0.98f}, {0.98f, 0.98f}, {0.98f, 0.46f}, {0.66f, 0.46f}})}, width, colour);
}

// ------------------------------------------------------------------ what a binding shows

// DualShock 2 inputs (libpad's order).
constexpr int kSelect = 0, kL3 = 1, kR3 = 2, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8,
              kR2 = 9, kL1 = 10, kR1 = 11, kTriangle = 12, kCircle = 13, kCross = 14, kSquare = 15;

/* How a cell is drawn: its mark's own colours (face buttons), or all one. */
enum class Tint { kOwn, kWhite, kDark, kGrey, kKey };

Colour TintColour(Tint tint) {
    switch (tint) {
        case Tint::kDark: return kDark;
        case Tint::kGrey: return kGrey;
        case Tint::kKey: return kKeyText;
        default: return kWhite;
    }
}

/* A key's cap: a mark, or its short name. */
void KeyCap(Canvas& c, const Box& cell, int vk, Tint tint, float stroke) {
    const Colour colour = TintColour(tint);
    const float part = tint == Tint::kGrey ? 0.85f : 0.78f;
    switch (vk) {
        case VK_RETURN: EnterMark(c, cell, colour, stroke); return;
        case VK_BACK: BackspaceMark(c, cell, colour, stroke); return;
        case VK_SPACE: SpaceMark(c, cell, colour, stroke); return;
        case VK_LSHIFT:
        case VK_RSHIFT: ShiftMark(c, cell, colour, stroke * 0.8f); return;
        case VK_UP: ArrowMark(c, cell, 0, colour); return;
        case VK_RIGHT: ArrowMark(c, cell, 1, colour); return;
        case VK_DOWN: ArrowMark(c, cell, 2, colour); return;
        case VK_LEFT: ArrowMark(c, cell, 3, colour); return;
        default: break;
    }
    std::string text;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) text = std::string(1, char(vk));
    else if (vk >= VK_F1 && vk <= VK_F12) text = "F" + std::to_string(vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) text = std::string(1, char('0' + vk - VK_NUMPAD0));
    else switch (vk) {
        case VK_LCONTROL: case VK_RCONTROL: text = "CTRL"; break;
        case VK_LMENU: case VK_RMENU: text = "ALT"; break;
        case VK_CAPITAL: text = "CAPS"; break;
        case VK_INSERT: text = "INS"; break;
        case VK_DELETE: text = "DEL"; break;
        case VK_HOME: text = "HOME"; break;
        case VK_END: text = "END"; break;
        case VK_PRIOR: text = "PGUP"; break;
        case VK_NEXT: text = "PGDN"; break;
        case VK_DECIMAL: text = "."; break;
        case VK_ADD: text = "+"; break;
        case VK_SUBTRACT: text = "-"; break;
        case VK_MULTIPLY: text = "*"; break;
        case VK_DIVIDE: text = "/"; break;
        case VK_OEM_1: text = ";"; break;
        case VK_OEM_PLUS: text = "="; break;
        case VK_OEM_COMMA: text = ","; break;
        case VK_OEM_MINUS: text = "-"; break;
        case VK_OEM_PERIOD: text = "."; break;
        case VK_OEM_2: text = "/"; break;
        case VK_OEM_3: text = "`"; break;
        case VK_OEM_4: text = "["; break;
        case VK_OEM_5: text = "\\"; break;
        case VK_OEM_6: text = "]"; break;
        case VK_OEM_7: text = "'"; break;
        default: text = "?"; break;
    }
    // The number pad's: NUM over the digit or sign.
    const bool numpad = (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) || vk == VK_DECIMAL || vk == VK_ADD ||
                        vk == VK_SUBTRACT || vk == VK_MULTIPLY || vk == VK_DIVIDE;
    if (numpad) {
        const float split = cell.y0 + cell.H() * 0.36f;
        Text(c, "NUM", cell.x0, cell.y0, cell.x1, split, colour, 0.8f);
        Text(c, text, cell.x0, split, cell.x1, cell.y1, colour, 0.82f);
        return;
    }
    Text(c, text, cell.x0, cell.y0, cell.x1, cell.y1, colour, part, text.size() == 1 ? 0.0f : 0.0f);
}

/* A controller's button `button` (by its DualShock 2 place) in the style. */
void Button(Canvas& c, const Box& cell, int button, Style style, Tint tint, float stroke) {
    const bool own = tint == Tint::kOwn || tint == Tint::kKey;
    const Colour plain = TintColour(tint);
    if (style == Style::kXbox) {
        // As large as the game's marks: the game draws the button's disc
        // round the cell. The top cell (Triangle's) sits high in its box -
        // the triangle's point is at its top - and a letter centred in it
        // looks high: it goes a texel lower.
        auto letter = [&](const char* text, Colour colour, float lower = 0.0f) {
            Text(c, text, cell.x0, cell.y0 + lower, cell.x1, cell.y1 + lower, own ? colour : plain, 0.9f,
                 stroke * 1.2f);
        };
        switch (button) {
            case kCross: letter("A", kXboxA); return;
            case kCircle: letter("B", kXboxB); return;
            case kSquare: letter("X", kXboxX); return;
            case kTriangle: letter("Y", kXboxY, 1.0f); return;
            case kStart: MenuMark(c, cell, plain, stroke); return;
            case kSelect: ViewMark(c, cell, plain, stroke); return;
            default: break;
        }
    } else {
        switch (button) {
            case kCross: CrossMark(c, cell, own ? kCrossBlue : plain, stroke); return;
            case kCircle: CircleMark(c, cell, own ? kCirclePink : plain, stroke); return;
            case kSquare: SquareMark(c, cell, own ? kSquareMagenta : plain, stroke); return;
            case kTriangle: TriangleMark(c, cell, own ? kTriangleTeal : plain, stroke); return;
            case kStart: StartMark(c, cell, plain, stroke, tint == Tint::kDark); return;
            case kSelect: SelectMark(c, cell, plain, stroke, tint == Tint::kDark); return;
            default: break;
        }
    }
    switch (button) {
        case kUp: ArrowMark(c, cell, 0, plain); return;
        case kRight: ArrowMark(c, cell, 1, plain); return;
        case kDown: ArrowMark(c, cell, 2, plain); return;
        case kLeft: ArrowMark(c, cell, 3, plain); return;
        default: break;
    }
    static const char* const kPlayStation[16] = {"SEL", "L3", "R3", "ST", "", "", "", "", "L2", "R2", "L1", "R1"};
    static const char* const kXbox[16] = {"", "LS", "RS", "", "", "", "", "", "LT", "RT", "LB", "RB"};
    if (button >= 0 && button < 12)
        Text(c, style == Style::kXbox ? kXbox[button] : kPlayStation[button], cell.x0, cell.y0, cell.x1, cell.y1,
             plain, 0.7f);
}

/* What one of the game's button slots shows: the device's binding for it. */
struct Look {
    Device device = Device::kController;
    Style style = Style::kPlayStation;
    bool mouse = false;             // the mouse's buttons for Cross and Triangle
};

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
    if (bound >= 0) Button(c, cell, bound, look.style, tint, stroke);
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
            if (face) Button(c, inside.Square(0.8f), bound, look.style, Tint::kOwn, 2.0f);
            else Button(c, inside, bound, look.style, Tint::kKey, 2.4f);
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
        if (face) Button(c, inside.Square(0.85f), bound, look.style, Tint::kOwn, 2.0f);
        else Button(c, inside, bound, look.style, Tint::kKey, 2.6f);
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
    if (mouse_used) g_prompts = Prompts::kMouse;
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
