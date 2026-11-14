/*
 * RenderWare's VU1 programs natively (docs/NATIVE_RENDERER.md; the engine's
 * docs/NATIVE_RENDERER.md, level B): a run of a program this knows is drawn by
 * the GPU from the game's own vertices (gs_mesh_rw.vert; its CPU twin
 * ps2host/rw_native.h), no microcode - the CPU deciding what the microcode would
 * (its path, the strip's triangles, what it leaves in VU1's memory and
 * registers).
 *
 * The cars' program (VU1 image 7E86891B; its code 0x0000-0x14EF, FNV-1a 64
 * 1C18B5ADDF2729D4): a batch of N <= 52 vertices at TOP, three quadwords each,
 * lit by a quadratic form of the normal, in two passes (the base, then the
 * sphere-mapped env pass in context 2) unless 1013.y says one; with 1013.x's
 * bit 1 (latched in vi12 when the transform is rebuilt) the guard band's ADC.
 * Not here: the skinning (1013.z; never seen) - the microcode's.
 *
 * PS2HOST_NATIVE_RW: on by default where the renderer draws meshes; 0 off;
 * check - every run the microcode's, the CPU twin's packet set against it word
 * for word (said every 20,000 runs and at the end).
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "ps2host/run.h"
#include "ps2host/rw_native.h"
#include "ps2host/vu.h"
#include "ps2host/vu_native.h"

namespace {

using ps2host::Vu;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using u8 = std::uint8_t;
using s32 = std::int32_t;
namespace rw = ps2host::rw_native;

constexpr u64 kCarsHash = 0x2DD10C0DBCB1806Cull;   // VuNativeHash of instruction memory 0x0000-0x14EF
constexpr u32 kCarsEnd = 0x14F0;
constexpr u32 kCarsResume = 0x14E0;                // (the E pair at 0x14D0: MSCNT continues at the stub)

enum class Mode { kOff, kOn, kCheck };
Mode TheMode() {
    static const Mode mode = [] {
        const char* e = std::getenv("PS2HOST_NATIVE_RW");
        if (e && std::string(e) == "0") return Mode::kOff;
        if (e && std::string(e) == "check") return Mode::kCheck;
        return Mode::kOn;
    }();
    return mode;
}

bool Holds(const Vu& vu) {
    if (TheMode() == Mode::kOff || (TheMode() == Mode::kOn && !ps2host::GsNativeDraws())) return false;
    return ps2host::VuNativeHash(vu.micro.data(), kCarsEnd, 0) == kCarsHash;
}

u32 Lo(const u32* mem, u32 quad, int lane) { return mem[quad * 4 + lane] & 0xFFFF; }
float Fl(const u32* mem, u32 quad, int lane) { return rw::F(mem[quad * 4 + lane]); }

constexpr u32 kPalette = 64;            // the most palette rows a skinned batch's header carries

// What a run of the cars' program decides from VU1's state as it starts.
struct Cars {
    u32 top, n, n4;
    bool rebuild, path_a, guard, prefix;
    float c[4][4], o[4];                // the transform (vf28-vf31) and vf02 - rebuilt, or as the registers hold them
    u32 vi12;
    float header[(rw::kRwHeaderQuads + kPalette) * 4];
    u32 header_quads;                   // (with the skinning's palette)
    bool skin;
    std::vector<u32> indices;           // the strip's triangles without ADC
    std::vector<u32> adc;               // per vertex: XYZF2's bit 15 as the microcode leaves it
    u32 clip_end;                       // the CLIP flag after the run
};

// A skinned vertex's place (the palette read from data memory).
void Skinned(const u32* mem, const u32* in, u32 v, float* p) {
    float n[3];
    rw::CarsSkin(in, v, Fl(mem, 970, 3), [&](u32 a) { return reinterpret_cast<const float*>(mem + (a & 0x3FF) * 4); }, p, n);
}

// -1: not this way (the microcode's).
int Decide(const Vu& vu, Cars* k) {
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    k->skin = Lo(mem, 1013, 2) != 0;                     // (the skinning: two bones a vertex)
    k->top = vu.top & 0x3FF;
    k->n = vu.itop & 0x3FF;
    if (k->n == 0 || k->n > 52) return -2;
    k->n4 = (k->n + 3) & ~3u;
    if (k->top + 3 * k->n4 + 1 > 1024) return -3;
    k->rebuild = Lo(mem, 965, 3) == 0;
    k->path_a = Lo(mem, 1013, 1) == 0;
    k->prefix = Lo(mem, 1006, 0) != 0;
    if (k->rebuild) {
        // 3.1: P (1014-1017) with z halved, through the object's columns (962-965), x and y less z, by S (1018)
        float P[4][4];
        for (int r = 0; r < 4; ++r)
            for (int l = 0; l < 4; ++l) P[r][l] = Fl(mem, 1014 + r, l);
        for (int r = 0; r < 4; ++r) P[r][2] = rw::Mul(P[r][2], 0.5f);
        float C[4][4];
        for (int col = 0; col < 4; ++col) {
            const float wx = Fl(mem, 962 + col, 0), wy = Fl(mem, 962 + col, 1), wz = Fl(mem, 962 + col, 2);
            for (int l = 0; l < 4; ++l) {
                float acc = rw::Mul(P[0][l], wx);
                acc = rw::Madd(acc, P[1][l], wy);
                acc = rw::Madd(acc, P[2][l], wz);
                if (col == 3) acc = rw::Madd(acc, P[3][l], 1.0f);
                C[col][l] = acc;
            }
        }
        const float sx = Fl(mem, 1018, 0), sy = Fl(mem, 1018, 1), sz = Fl(mem, 1018, 2);
        for (int col = 0; col < 4; ++col) {
            k->c[col][0] = rw::Mul(rw::Sub(C[col][0], C[col][2]), sx);
            k->c[col][1] = rw::Mul(rw::Sub(C[col][1], C[col][2]), sy);
            k->c[col][2] = col == 3 ? rw::Mul(1.0f, sz) : 0.0f;
            k->c[col][3] = C[col][3];
        }
        for (int l = 0; l < 3; ++l) k->o[l] = Fl(mem, 1019, l);
        k->o[3] = 0.0f;
        k->vi12 = Lo(mem, 1013, 0) & 2;
    } else {
        for (int col = 0; col < 4; ++col) std::memcpy(k->c[col], vu.regs.vf[28 + col], 16);
        std::memcpy(k->o, vu.regs.vf[2], 16);
        k->vi12 = vu.regs.vc[12] & 0xFFFF;
    }
    k->guard = k->vi12 != 0;
    // the header
    float* h = k->header;
    std::memset(h, 0, sizeof k->header);
    for (int col = 0; col < 4; ++col) std::memcpy(h + 4 * (rw::kRwC + col), k->c[col], 16);
    std::memcpy(h + 4 * rw::kRwO, k->o, 16);
    std::memcpy(h + 4 * rw::kRwM, mem + 966 * 4, 8 * 16);           // 966-973
    std::memcpy(h + 4 * rw::kRwC0, mem + 1010 * 4, 3 * 16);         // 1010-1012
    std::memcpy(h + 4 * rw::kRwGuard, mem + 1022 * 4, 2 * 16);      // 1022-1023
    k->header_quads = rw::kRwHeaderQuads;
    if (k->skin) {
        // the palette rows the batch's words reach (974 + each, four rows; addresses mod 1024), as one window
        u32 lo = 0x400, hi = 0;
        const u32* in = mem + k->top * 4;
        for (u32 g = 0; g < k->n4 / 4; ++g)
            for (u32 l = 0; l < 4; ++l)
                for (u32 which : {1u, 3u}) {
                    const u32 a = (974 + (in[12 * (4 * g + which) + 4 + l] & 0xFFFF)) & 0x3FF;
                    if (a + 3 > 0x3FF) return -1;
                    lo = std::min(lo, a), hi = std::max(hi, a + 3);
                }
        if (hi - lo + 1 > kPalette) return -1;
        std::memcpy(h + 4 * rw::kRwMisc + 3, &lo, 4);
        std::memcpy(h + 4 * rw::kRwHeaderQuads, mem + lo * 4, std::size_t(hi - lo + 1) * 16);
        k->header_quads = rw::kRwHeaderQuads + (hi - lo + 1);
    }
    const bool t4 = k->guard && k->path_a;
    if (t4) {
        // the guard band's matrix: G_k = (ck.x igx, ck.y igy, zB ck.w, ck.w); G_3's z zA + zB c3.w
        const float igx = Fl(mem, 1022, 1), igy = Fl(mem, 1023, 1), zA = Fl(mem, 1022, 2), zB = Fl(mem, 1023, 2);
        for (int col = 0; col < 4; ++col) {
            float* g = h + 4 * (rw::kRwG + col);
            g[0] = rw::Mul(k->c[col][0], igx);
            g[1] = rw::Mul(k->c[col][1], igy);
            g[2] = col == 3 ? rw::Madd(rw::Add(0.0f, zA), zB, k->c[col][3]) : rw::Mul(zB, k->c[col][3]);
            g[3] = rw::Mul(k->c[col][3], 1.0f);
        }
    }
    // the strip: ADC from the vertex's own mark, and (the guard band) any of the last three judgements out
    const u32* in = mem + k->top * 4;
    k->adc.assign(k->n, 0);
    u32 clip = vu.clip & 0xFFFFFF;
    for (u32 v = 0; v < k->n; ++v) {
        const u32* q0 = in + 12 * v;
        bool hit = false;
        if (k->guard) {
            float p[3] = {rw::F(q0[0]), rw::F(q0[1]), rw::F(q0[2])};
            if (k->skin) Skinned(mem, in, v, p);
            u32 j;
            if (t4) {
                float g[4];
                rw::GuardOf(h, p, g);
                j = rw::Judge(g[0], g[1], g[2], g[3]);
            } else {
                float cl[4];
                rw::ClipOf(h, p, cl);
                j = rw::JudgeT2(h, cl);
            }
            clip = ((clip << 6) | j) & 0xFFFFFF;
            hit = (clip & 0x3FFFF) != 0;
        }
        k->adc[v] = (hit || (q0[3] & 0x8000)) ? 1u : 0u;
    }
    if (t4) {
        // (T4 judges one more: the quadword after the batch, as memory has it)
        const u32* q = in + 12 * k->n;
        float p[3] = {rw::F(q[0]), rw::F(q[1]), rw::F(q[2])};
        if (k->skin && k->n < k->n4) Skinned(mem, in, k->n, p);     // (a padding vertex: skinned in place)
        float g[4];
        rw::GuardOf(h, p, g);
        clip = ((clip << 6) | rw::Judge(g[0], g[1], g[2], g[3])) & 0xFFFFFF;
    }
    k->clip_end = k->guard ? clip : (vu.clip & 0xFFFFFF);
    k->indices.clear();
    for (u32 v = 2; v < k->n; ++v) {
        if (k->adc[v]) continue;
        k->indices.push_back(v - 2);
        k->indices.push_back(v - 1);
        k->indices.push_back(v);
    }
    return 1;
}

void SetMode(float* h, u32 mode) { std::memcpy(h + 4 * rw::kRwMisc + 2, &mode, 4); }

// ---------------------------------------------------------------- the check

struct CheckStats {
    u64 runs = 0, words = 0, differ = 0, adc_differ = 0, clip_differ = 0, regs_differ = 0, told = 0;
};
CheckStats& TheCheck() { static CheckStats s; return s; }

void SayCheck() {
    const CheckStats& s = TheCheck();
    std::fprintf(stderr, "[rw native] cars check: %llu runs, %llu packet words compared, %llu differ; ADC %llu, CLIP %llu, registers %llu\n",
                 (unsigned long long)s.runs, (unsigned long long)s.words, (unsigned long long)s.differ,
                 (unsigned long long)s.adc_differ, (unsigned long long)s.clip_differ, (unsigned long long)s.regs_differ);
}

void Check(Vu& vu, u32 pc, u64* pairs) {
    static thread_local std::vector<u32> before;
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    Cars k;
    const int decided = Decide(vu, &k);
    before.assign(mem, mem + 4096);
    const u32 vi13 = vu.regs.vc[13] & 0xFFFF, vi14 = vu.regs.vc[14] & 0xFFFF;
    *pairs = vu.RunEmulated(pc, u64(1) << 40);
    if (decided < 0) return;
    CheckStats& s = TheCheck();
    ++s.runs;
    const u32 b = vi14;             // (the buffer filled and kicked: vi13 after the swap)
    if ((vu.regs.vc[13] & 0xFFFF) != b || (vu.regs.vc[14] & 0xFFFF) != vi13) ++s.regs_differ;
    if (k.rebuild) {
        for (int col = 0; col < 4; ++col)
            if (std::memcmp(k.c[col], vu.regs.vf[28 + col], 16) != 0) ++s.regs_differ;
        if (std::memcmp(k.o, vu.regs.vf[2], 12) != 0 || (vu.regs.vc[12] & 0xFFFF) != k.vi12) ++s.regs_differ;
    }
    if ((vu.clip & 0xFFFFFF) != k.clip_end) ++s.clip_differ;
    const u32* out = mem + b * 4;
    for (int stream = 0; stream < (k.path_a ? 2 : 1); ++stream) {
        SetMode(k.header, (stream ? rw::kRwEnvStream : 0u) | (k.guard && k.path_a ? rw::kRwGuardPlace : 0u) |
                              (k.skin ? rw::kRwSkin : 0u));
        for (u32 v = 0; v < k.n; ++v) {
            rw::GsVertex o;
            rw::CarsVertex(k.header, before.data() + k.top * 4, v, &o);
            const u32* w = out + 4 * ((stream ? 2 + 3 * k.n : 1) + 3 * v);
            const u32 mine[10] = {o.stq[0], o.stq[1], o.stq[2], o.rgba[0], o.rgba[1], o.rgba[2], o.rgba[3],
                                  o.xyz[0], o.xyz[1], o.xyz[2]};
            const u32 theirs[10] = {w[0], w[1], w[2], w[4], w[5], w[6], w[7], w[8], w[9], w[10]};
            for (int i = 0; i < 10; ++i) {
                ++s.words;
                if (mine[i] != theirs[i]) {
                    ++s.differ;
                    if (s.told++ < 12)
                        std::fprintf(stderr, "[rw native] cars check: run %llu stream %d vertex %u word %d: twin %08X, microcode %08X (path %s%s)\n",
                                     (unsigned long long)s.runs, stream, v, i, mine[i], theirs[i], k.path_a ? "A" : "B",
                                     k.guard ? ", guard" : "");
                }
            }
            if (((w[11] >> 15) & 1) != k.adc[v] && v >= 2) {
                if (s.adc_differ++ < 6)
                    std::fprintf(stderr, "[rw native] cars check: run %llu vertex %u ADC: twin %u, microcode %u\n",
                                 (unsigned long long)s.runs, v, k.adc[v], (w[11] >> 15) & 1);
            }
        }
    }
    if (s.runs % 20000 == 0) SayCheck();
}

// ---------------------------------------------------------------- the native run

void Declined(const char* program, int reason);

struct State {
    std::unordered_set<u64> uploaded;
    u64 uploaded_quads = 0;
    u64 runs = 0;
};
State& TheState() { static State s; return s; }

u64 Hash(u64 h, const u32* p, size_t words) { return ps2host::VuMeshHash(p, words * 4, h); }

void SendSized(const Cars& k, const float* header, u32 hq, u32 flags, u64 key, u64 prim, const u32* inputs) {
    const u32 ni = u32(k.indices.size());
    const u32 upload = (flags & 1) ? 1 + 12 * k.n4 : 0;
    const u32 count = 7 + 4 * hq + ni + upload;
    u32* w = ps2host::GsNativeBegin(count);
    w[0] = flags, w[1] = u32(key), w[2] = u32(key >> 32), w[3] = u32(prim), w[4] = u32(prim >> 32);
    w[5] = hq, w[6] = ni;
    std::memcpy(w + 7, header, std::size_t(hq) * 16);
    if (ni) std::memcpy(w + 7 + 4 * hq, k.indices.data(), std::size_t(ni) * 4);
    if (upload) {
        u32* m = w + 7 + 4 * hq + ni;
        m[0] = 3 * k.n4;
        std::memcpy(m + 1, inputs, std::size_t(12) * k.n4 * 4);
    }
    ps2host::GsNativeEnd(count);
}

void Send(const Cars& k, const float* header, u32 flags, u64 key, u64 prim, const u32* inputs) {
    SendSized(k, header, rw::kRwHeaderQuads, flags, key, prim, inputs);
}

// The world's: its mesh `quads` quadwords from TOP (1332's normals past the vertices).
void SendWorld(const u32* indices, u32 ni, const float* header, u32 flags, u64 key, u64 prim, const u32* inputs, u32 quads) {
    const u32 hq = rw::kRwWHeaderQuads;
    const u32 upload = (flags & 1) ? 1 + 4 * quads : 0;
    const u32 count = 7 + 4 * hq + ni + upload;
    u32* w = ps2host::GsNativeBegin(count);
    w[0] = flags, w[1] = u32(key), w[2] = u32(key >> 32), w[3] = u32(prim), w[4] = u32(prim >> 32);
    w[5] = hq, w[6] = ni;
    std::memcpy(w + 7, header, std::size_t(hq) * 16);
    if (ni) std::memcpy(w + 7 + 4 * hq, indices, std::size_t(ni) * 4);
    if (upload) {
        u32* m = w + 7 + 4 * hq + ni;
        m[0] = quads;
        std::memcpy(m + 1, inputs, std::size_t(quads) * 16);
    }
    ps2host::GsNativeEnd(count);
}

bool Native(Vu& vu, u64* pairs) {
    static thread_local Cars k;         // (reused: no allocation a run)
    if (const int why = Decide(vu, &k); why < 0) {
        Declined("cars", why);
        return false;
    }
    u32* mem = reinterpret_cast<u32*>(vu.data.data());
    const u32 vi13 = vu.regs.vc[13] & 0xFFFF, vi14 = vu.regs.vc[14] & 0xFFFF;
    if (vi13 + 2 + 6 * k.n > 1024 || vi14 + 2 + 6 * k.n > 1024 || vi14 < 4) {
        Declined("cars", -6);
        return false;
    }
    // what the run leaves: the transform, 965.w, the tags' words 1-3 in both buffers, the buffers swapped
    if (k.rebuild) {
        for (int col = 0; col < 4; ++col) std::memcpy(vu.regs.vf[28 + col], k.c[col], 16);
        std::memcpy(vu.regs.vf[2], k.o, 12);
        vu.regs.vc[12] = k.vi12;
        vu.regs.vc[11] = Lo(mem, 1013, 0) & 1;
        mem[965 * 4 + 3] = 1;
    }
    for (u32 b : {vi13, vi14}) {
        mem[(b + 0) * 4 + 1] = 0x302E4000u, mem[(b + 0) * 4 + 2] = 0x00000412u, mem[(b + 0) * 4 + 3] = 0x3F800000u;
    }
    const u32 b = vi14;
    vu.regs.vc[13] = b, vu.regs.vc[14] = vi13;
    // the prefix (1006-1009 before the packet, once): kicked alone (its tag's EOP set for the kick)
    if (k.prefix) {
        std::memcpy(mem + (b - 4) * 4, mem + 1006 * 4, 64);
        const u32 tag = mem[(b - 4) * 4];
        mem[(b - 4) * 4] = tag | 0x8000u;
        ps2host::VuNativeAccess::Kick(vu, b - 4);
        mem[(b - 4) * 4] = tag;
        mem[1006 * 4] &= 0xFFFF0000u;
    }
    // the mesh: the batch's vertices (to the group of four's end), as VIF1 left them
    const u32* inputs = mem + k.top * 4;
    State& st = TheState();
    const u64 key = Hash(0xC2B2AE3D27D4EB4Full ^ k.n, inputs, std::size_t(12) * k.n4) | (u64(3) << 62);
    u32 flags = 0x100 | 0x200 | 4;
    if (!st.uploaded.count(key)) {
        if (st.uploaded.size() >= 20000 || st.uploaded_quads > (u64(16) << 20) / 16) {
            flags |= 2;
            st.uploaded.clear();
            st.uploaded_quads = 0;
        }
        flags |= 1;
        st.uploaded.insert(key);
        st.uploaded_quads += u64(3) * k.n4;
    }
    const bool t4 = k.guard && k.path_a;
    const u32 skin = k.skin ? rw::kRwSkin : 0u;
    SetMode(k.header, (t4 ? rw::kRwGuardPlace : 0u) | skin);
    SendSized(k, k.header, k.header_quads, flags, key, 0x5C, inputs);
    if (k.path_a) {
        SetMode(k.header, rw::kRwEnvStream | (t4 ? rw::kRwGuardPlace : 0u) | skin);
        SendSized(k, k.header, k.header_quads, 0x100 | 0x200 | 4, key, 0x25C, inputs);
    }
    ps2host::VuNativeAccess::SetClip(vu, k.clip_end);
    vu.regs.vc[15] = 4;
    vu.resume = kCarsResume;
    // (the microcode's pairs, near enough: the statistics')
    *pairs = 100 + u64(k.n) * (k.path_a ? 30 : 18);
    ++st.runs;
    return true;
}

bool Run(Vu& vu, u32 pc, u64* pairs) {
    if (pc != 0x14E0 && pc != 0x0020) {
        if (pc != 0) Declined("cars", -7);
        return false;
    }
    if (pc == 0x14E0 && (vu.regs.vc[15] & 0xFFFF) != 4) {
        Declined("cars", -8);
        return false;
    }
    if (TheMode() == Mode::kCheck) {
        Check(vu, pc, pairs);
        return true;
    }
    return Native(vu, pairs);
}

// ---------------------------------------------------------------- the instanced strips (VU1 D4B61373, 34C3CF67)
//
// A batch (N <= 76 vertices at TOP) drawn once an instance (932.x + 1 of them, blocks of five quadwords from 934):
// each instance's matrix the camera's (1014-1017) by its rows, its colour 938 (or path C's light), path B (937.z:
// the guard band's ADC) or C (932.y: the lighting); one packet an instance, the output buffers swapped after each.
// Entered at 0x0020 (after the init at 0x0000) or 0x0EE8 (the next batch: 932.w = 0); ends at the E of 0x0ED8.

constexpr u64 kInstancedHash = 0xDEE6A6847ADC4DBFull;  // VuNativeHash of instruction memory 0x0000-0x0EEF
constexpr u32 kInstancedEnd = 0xEF0;
constexpr u32 kInstancedResume = 0x0EE8;

bool HoldsInstanced(const Vu& vu) {
    if (TheMode() == Mode::kOff || (TheMode() == Mode::kOn && !ps2host::GsNativeDraws())) return false;
    return ps2host::VuNativeHash(vu.micro.data(), kInstancedEnd, 0) == kInstancedHash;
}

struct Instance {
    float header[rw::kRwIHeaderQuads * 4];
    std::vector<u32> indices;
    std::vector<u32> adc;
};

struct Batch {
    u32 top, n, n4, prim, count;
    std::vector<Instance> instances;
    u32 clip_end;
};

s32 S16(u32 v) { return s32(std::int16_t(v & 0xFFFF)); }

// The batch's instances as the microcode would draw them (932.w 0: from the first); -1: not this way.
int DecideInstanced(const Vu& vu, Batch* b) {
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    b->top = vu.top & 0x3FF;
    b->n = vu.itop & 0x3FF;
    if (b->n == 0 || b->n > 76) return -1;           // (the reasons: Declined's)
    b->n4 = (b->n + 3) & ~3u;
    if (b->top + 3 * (b->n4 + 3) > 1024) return -2;
    const u32 tag1 = mem[933 * 4 + 1];
    if (!((tag1 >> 14) & 1) || mem[933 * 4 + 2] != 0x412) return -3;   // (PRE, and ST RGBAQ XYZF2)
    b->prim = (tag1 >> 15) & 0x7FF;
    const bool lit = Lo(mem, 932, 1) != 0;
    const s32 last = S16(mem[932 * 4]);
    b->count = last > 0 ? u32(last) + 1 : 1;
    if (b->count > 15) return -4;
    float V[4][4], S[4], off[4];
    for (int r = 0; r < 4; ++r) std::memcpy(V[r], mem + (1014 + r) * 4, 16);
    std::memcpy(S, mem + 1018 * 4, 16);
    std::memcpy(off, mem + 1019 * 4, 16);
    const float far = Fl(mem, 1022, 3), near = Fl(mem, 1023, 3);
    const u32* in = mem + b->top * 4;
    u32 clip = vu.clip & 0xFFFFFF;
    b->instances.resize(b->count);
    s32 last_b = -1;
    for (u32 i = 0; i < b->count; ++i) {
        const u32 block = 934 + 5 * i;
        const bool path_b = Lo(mem, block + 3, 2) != 0;
        if (path_b && lit) return -5;                   // (C with B: never seen)
        if (path_b) last_b = s32(i);
        Instance& it = b->instances[i];
        float* h = it.header;
        std::memset(h, 0, sizeof it.header);
        float R[4][4], M[4][4];
        for (int r = 0; r < 4; ++r) std::memcpy(R[r], mem + (block + r) * 4, 16);
        rw::InstanceMatrix(V, R, S, far, near, path_b, M);
        const u32 mode = rw::kRwInstanced | (path_b ? rw::kRwPathB : 0u) | (lit ? rw::kRwPathC : 0u);
        SetMode(h, mode);
        for (int r = 0; r < 4; ++r) std::memcpy(h + 4 * (rw::kRwIM + r), M[r], 16);
        std::memcpy(h + 4 * rw::kRwIS, S, 16);
        std::memcpy(h + 4 * rw::kRwIOff, off, 16);
        std::memcpy(h + 4 * rw::kRwIColour, mem + (block + 4) * 4, 16);
        h[4 * rw::kRwIAB + 0] = Fl(mem, block, 2);
        h[4 * rw::kRwIAB + 1] = Fl(mem, block + 1, 2);
        it.adc.assign(b->n, 0);
    }
    if (last_b >= 0) {
        // Path B's judgements, CLIP as the microcode leaves it: two of vf00, then each vertex of the groups of four
        // and three past them (the loop's pipelining). Those three read memory as the run has written it by then -
        // a full buffer's are this run's own packet (its tag, its first vertices' XYZF2) - so each of their
        // quadwords is the packet word last written there (the instance writing, or the one before into that
        // buffer), judged with the VU's own arithmetic.
        const u32 o13 = vu.regs.vc[13] & 0x3FF, o14 = vu.regs.vc[14] & 0x3FF;
        const u32 size = 1 + 3 * b->n4;             // (the quadwords an instance writes: its padding too)
        const u32 early = b->n4 > 4 ? b->n4 - 4 : 0; // (the vertices it has written when the three are read)
        auto quad = [&](u32 q, s32 i, u32* out) {
            q &= 0x3FF;
            const u32 mine = (i & 1) ? o14 : o13, other = (i & 1) ? o13 : o14;
            s32 writer;
            u32 base;
            if (q >= mine && q < mine + size) writer = i, base = mine;
            else if (q >= other && q < other + size) writer = i - 1, base = other;
            else writer = -1, base = 0;
            std::memcpy(out, mem + q * 4, 16);
            for (; writer >= 0; writer -= 2) {
                const u32 t = q - base;
                if (t == 0) {
                    const u32 tag[4] = {0xFFFF8000u | b->n, mem[933 * 4 + 1], mem[933 * 4 + 2], mem[933 * 4 + 3]};
                    std::memcpy(out, tag, 16);
                    return;
                }
                const u32 v = (t - 1) / 3;
                if (writer == i && v >= early) continue;    // (not yet: what the one before left)
                rw::GsVertex o;
                rw::InstancedVertex(b->instances[std::size_t(writer)].header, in, v, &o);
                const u32 slot = (t - 1) % 3;
                if (slot == 0) std::memcpy(out, o.stq, 12);
                else if (slot == 1) std::memcpy(out, o.rgba, 16);
                else std::memcpy(out, o.xyz, 12);
                return;
            }
        };
        for (s32 i = 0; i <= last_b; ++i) {
            Instance& it = b->instances[std::size_t(i)];
            if (!(rw::B(it.header[2]) & rw::kRwPathB)) continue;
            clip = (clip << 12) & 0xFFFFFF;
            u32 recent = 0;
            for (u32 v = 0; v < b->n4 + 3; ++v) {
                u32 j;
                if (v < b->n4) {
                    const u32* q0 = in + 12 * v;
                    const float p[3] = {rw::F(q0[0]), rw::F(q0[1]), rw::F(q0[2])};
                    float c[4];
                    rw::InstancedClip(it.header, p, true, c);
                    j = rw::Judge(c[0], c[1], c[2], c[3]);
                } else {
                    u32 q0[4], c[4];
                    quad(b->top + 3 * v, i, q0);
                    rw::InstancedClipVu(it.header, q0, true, c);
                    j = rw::JudgeVu(c);
                }
                clip = ((clip << 6) | j) & 0xFFFFFF;
                recent = ((recent << 6) | j) & 0x3FFFF;
                if (v < b->n && recent) it.adc[v] = 1;
            }
        }
    }
    for (Instance& it : b->instances) {
        it.indices.clear();
        for (u32 v = 0; v < b->n; ++v) {
            if (in[12 * v + 3] & 0x8000) it.adc[v] = 1;
            if (v >= 2 && !it.adc[v]) {
                it.indices.push_back(v - 2);
                it.indices.push_back(v - 1);
                it.indices.push_back(v);
            }
        }
    }
    b->clip_end = clip;
    return 1;
}

void CheckInstanced(Vu& vu, u32 pc, u64* pairs) {
    static thread_local std::vector<u32> before;
    u32* mem = reinterpret_cast<u32*>(vu.data.data());
    Batch b;
    int decided = -1;
    if (pc == 0x0EE8 || Lo(mem, 932, 3) == 0) {
        const u32 w932 = mem[932 * 4 + 3];
        mem[932 * 4 + 3] &= 0xFFFF0000u;        // (as 0x0EE8 does first; 0x0020's is 0 already)
        decided = DecideInstanced(vu, &b);
        mem[932 * 4 + 3] = w932;
    }
    before.assign(mem, mem + 4096);
    std::vector<u32> kicked;
    auto saved_block = vu.on_kick_block;
    auto saved = vu.on_kick;
    if (saved_block)
        vu.on_kick_block = [&](const u32* w, u32 c) { kicked.insert(kicked.end(), w, w + c); saved_block(w, c); };
    else
        vu.on_kick = [&](u64 lo, u64 hi) {
            const u32 q[4] = {u32(lo), u32(lo >> 32), u32(hi), u32(hi >> 32)};
            kicked.insert(kicked.end(), q, q + 4);
            if (saved) saved(lo, hi);
        };
    *pairs = vu.RunEmulated(pc, u64(1) << 40);
    vu.on_kick_block = saved_block;
    vu.on_kick = saved;
    if (decided < 0) return;
    CheckStats& s = TheCheck();
    ++s.runs;
    const std::size_t per = std::size_t(4) * (1 + 3 * b.n);
    if (kicked.size() != per * b.count) {
        if (s.told++ < 12)
            std::fprintf(stderr, "[rw native] instanced check: run %llu kicked %zu words, the twin %zu\n",
                         (unsigned long long)s.runs, kicked.size(), per * b.count);
        ++s.differ;
        return;
    }
    if ((vu.clip & 0xFFFFFF) != b.clip_end && s.clip_differ++ < 8) {
        u32 paths = 0;
        for (const Instance& it : b.instances) paths = paths * 10 + (rw::B(it.header[2]) & 3);
        std::fprintf(stderr, "[rw native] instanced CLIP: microcode %06X twin %06X, n %u, instances %u, paths %u\n",
                     vu.clip & 0xFFFFFF, b.clip_end, b.n, b.count, paths);
    }
    if (Lo(mem, 932, 2) != 0xFFFF || Lo(mem, 932, 3) != 5 * b.count) ++s.regs_differ;
    for (u32 i = 0; i < b.count; ++i) {
        const u32* packet = kicked.data() + per * i;
        if (((packet[1] >> 15) & 0x7FF) != b.prim) ++s.differ;
        for (u32 v = 0; v < b.n; ++v) {
            rw::GsVertex o;
            rw::InstancedVertex(b.instances[i].header, before.data() + b.top * 4, v, &o);
            const u32* w = packet + 4 + 12 * v;
            const u32 mine[10] = {o.stq[0], o.stq[1], o.stq[2], o.rgba[0], o.rgba[1], o.rgba[2], o.rgba[3],
                                  o.xyz[0], o.xyz[1], o.xyz[2]};
            const u32 theirs[10] = {w[0], w[1], w[2], w[4], w[5], w[6], w[7], w[8], w[9], w[10]};
            for (int x = 0; x < 10; ++x) {
                ++s.words;
                if (mine[x] != theirs[x]) {
                    ++s.differ;
                    if (s.told++ < 12)
                        std::fprintf(stderr, "[rw native] instanced check: run %llu instance %u vertex %u word %d: twin %08X, microcode %08X (mode %X)\n",
                                     (unsigned long long)s.runs, i, v, x, mine[x], theirs[x],
                                     rw::B(b.instances[i].header[2]));
                }
            }
            if (v >= 2 && ((w[11] >> 15) & 1) != b.instances[i].adc[v] && s.adc_differ++ < 6)
                std::fprintf(stderr, "[rw native] instanced check: run %llu instance %u vertex %u ADC differs\n",
                             (unsigned long long)s.runs, i, v);
        }
    }
    if (s.runs % 20000 == 0) SayCheck();
}

// PS2HOST_NATIVE_RW_STATS=1: why runs were left to the microcode, every 10,000.
void Declined(const char* program, int reason) {
    static const bool say = std::getenv("PS2HOST_NATIVE_RW_STATS") != nullptr;
    static u64 counts[3][16] = {}, all = 0;
    const int which = program[0] == 'c' ? 0 : program[0] == 'i' ? 1 : 2;
    ++counts[which][std::min(15, -reason)];
    if (say && ++all % 10000 == 0) {
        std::fprintf(stderr, "[rw native] declined:");
        static const char* const names[3] = {"cars", "instanced", "world"};
        for (int w = 0; w < 3; ++w)
            for (int r = 0; r < 16; ++r)
                if (counts[w][r]) std::fprintf(stderr, " %s %d: %llu", names[w], -r, (unsigned long long)counts[w][r]);
        std::fprintf(stderr, "\n");
    }
}

bool NativeInstanced(Vu& vu, u32 pc, u64* pairs) {
    u32* mem = reinterpret_cast<u32*>(vu.data.data());
    if (pc == 0x0020 && Lo(mem, 932, 3) != 0) {
        Declined("instanced", -10);
        return false;
    }
    const u32 w932 = mem[932 * 4 + 3];
    mem[932 * 4 + 3] &= 0xFFFF0000u;            // (0x0EE8's first: 932.w = 0)
    static thread_local Batch b;
    if (const int why = DecideInstanced(vu, &b); why < 0) {
        Declined("instanced", why);
        mem[932 * 4 + 3] = w932;
        return false;
    }
    const u32* inputs = mem + b.top * 4;
    State& st = TheState();
    const u64 key = Hash(0x165667B19E3779F9ull ^ b.n, inputs, std::size_t(12) * b.n4) | (u64(3) << 62);
    u32 vi13 = vu.regs.vc[13] & 0xFFFF, vi14 = vu.regs.vc[14] & 0xFFFF;
    static thread_local Cars shape;             // (SendSized's: the batch's size, an instance's indices)
    shape.n = b.n, shape.n4 = b.n4;
    for (u32 i = 0; i < b.count; ++i) {
        u32 flags = 0x100 | 0x200 | 4;
        if (i == 0 && !st.uploaded.count(key)) {
            if (st.uploaded.size() >= 20000 || st.uploaded_quads > (u64(16) << 20) / 16) {
                flags |= 2;
                st.uploaded.clear();
                st.uploaded_quads = 0;
            }
            flags |= 1;
            st.uploaded.insert(key);
            st.uploaded_quads += u64(3) * b.n4;
        }
        shape.indices.swap(b.instances[i].indices);
        SendSized(shape, b.instances[i].header, rw::kRwIHeaderQuads, flags, key, b.prim, inputs);
        std::swap(vi13, vi14);
    }
    vu.regs.vc[13] = vi13, vu.regs.vc[14] = vi14;
    mem[932 * 4 + 2] = 0xFFFF;
    mem[932 * 4 + 3] = 5 * b.count;
    ps2host::VuNativeAccess::SetClip(vu, b.clip_end);
    vu.resume = kInstancedResume;
    *pairs = 60 + u64(b.count) * (50 + 36 * u64(b.n4));
    ++st.runs;
    return true;
}

bool RunInstanced(Vu& vu, u32 pc, u64* pairs) {
    if (pc != 0x0020 && pc != 0x0EE8) {
        if (pc != 0) Declined("instanced", -7);
        return false;
    }
    if (TheMode() == Mode::kCheck) {
        CheckInstanced(vu, pc, pairs);
        return true;
    }
    return NativeInstanced(vu, pc, pairs);
}

// ---------------------------------------------------------------- the world's strips (VU1 2678EB26, 1332C99F, 01CE30D5)
//
// RenderWare's strip pipeline in three images (build/native/spec_2678EB26.md, spec_1332C99F.md, spec_01CE30D5.md):
// a batch of N vertices at TOP drawn as one strip, the path chosen by 1013.x (cached in 1013.y; 1013.w != 0 the
// transform rebuilt into vf28-vf31, vf02). Paths A and B here (B: the guard band's ADC); C - fog and the six
// planes' clipper - and the paths that draw nothing are the microcode's.

struct WorldImage {
    const char* name;
    u64 hash;                   // VuNativeHash of instruction memory [0, end)
    u32 end, stub;              // (the stub: MSCNT continues there, `jr vi15` to 0x0020)
    u32 vi12_a, vi12_b, vi12_c; // the dispatch's jump targets (pairs) of paths A, B and C
    bool cull;                  // the screen winding's ADC (2678's, 1332's)
    bool lit;                   // 1332's specular alpha (its normals at TOP + 0xCC)
    u32 most;                   // the most vertices a batch has
    u32 packet;                 // path C's packets' vertices (a flush past it)
    bool mirror = false;        // 0835's: the transform reflected about y = 1009.y for the run (spec_0835A68B.md)
};
const WorldImage kWorldImages[4] = {
    {"RenderWare's world strips (Burnout 3, VU1 2678EB26)", 0x4A6CAC64BC3CF693ull, 0x1558, 0x1548, 0x057, 0x0A0, 0x0F3, true, false, 80, 57},
    {"RenderWare's lit strips (Burnout 3, VU1 1332C99F)", 0x83D58831DEC17E8Eull, 0x1920, 0x1910, 0x0D0, 0x119, 0x16C, true, true, 68, 46},
    {"RenderWare's prelit strips (Burnout 3, VU1 01CE30D5)", 0xA39D4CE658CE3C36ull, 0x1328, 0x1318, 0x057, 0x081, 0x0B2, false, false, 80, 57},
    {"RenderWare's mirrored prelit strips (Burnout 3, VU1 0835A68B)", 0x35A220CC8F5E80D8ull, 0x13A8, 0x1398, 0x05F, 0x089, 0x0BA, false, false, 80, 57, true},
};
constexpr u32 kNormals = 0xCC;          // (1332's normal block: TOP + 0xCC, four quadwords a group of four)

template <int I>
bool HoldsWorld(const Vu& vu) {
    if (TheMode() == Mode::kOff || (TheMode() == Mode::kOn && !ps2host::GsNativeDraws())) return false;
    return ps2host::VuNativeHash(vu.micro.data(), kWorldImages[I].end, 0) == kWorldImages[I].hash;
}

struct World {
    u32 top, n, n4, quads;
    bool rebuild, path_b, path_c;
    float t[4][4], o[4];
    float regs[4][4];                   // (the transform as the registers keep it: 0835's unmirrored)
    u32 vi12;
    float header[rw::kRwWHeaderQuads * 4];
    std::vector<u32> indices;
};

// The dispatch's target from 1013.x (all three images alike; their targets their own).
u32 WorldTarget(const WorldImage& img, u32 x, u32 c_target) {
    if (!(x & 2)) return (x & 1) ? 0 : img.vi12_a;
    if ((x & 8) && !(x & 4)) return c_target;
    return (x & 1) ? 0 : img.vi12_b;
}

int DecideWorld(const Vu& vu, const WorldImage& img, World* w) {
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    if (Lo(mem, 1005, 0) != 0) return -11;                 // (the S scroll: never seen)
    w->rebuild = Lo(mem, 1013, 3) != 0;
    w->vi12 = w->rebuild ? WorldTarget(img, Lo(mem, 1013, 0), img.vi12_c) : Lo(mem, 1013, 1);
    w->path_b = w->vi12 == img.vi12_b;
    w->path_c = w->vi12 == img.vi12_c;
    if (w->vi12 != img.vi12_a && !w->path_b && !w->path_c) return -13;   // (a path drawing nothing)
    w->top = vu.top & 0x3FF;
    w->n = vu.itop & 0x3FF;
    if (w->n < 3 || w->n > img.most) return -14;
    w->n4 = (w->n + 3) & ~3u;
    w->quads = img.lit ? kNormals + w->n4 : 3 * w->n4;
    if (w->top + w->quads > 1024 || w->top + 3 * ((w->n + 1) & ~1u) + 1 > 1024) return -15;
    if (w->rebuild) {
        // vf28-vf31 = 1014-1017's x, y, w (z: 0, 0, 0, 1), less half their w in x and y, by 1018; vf02 = 1019
        for (int r = 0; r < 4; ++r) {
            for (int l : {0, 1, 3}) w->t[r][l] = Fl(mem, 1014 + r, l);
            w->t[r][2] = r == 3 ? 1.0f : 0.0f;
            const float h = rw::Mul(w->t[r][3], 0.5f);
            w->t[r][0] = rw::Sub(w->t[r][0], h);
            w->t[r][1] = rw::Sub(w->t[r][1], h);
        }
        for (int r = 0; r < 4; ++r) {
            w->t[r][0] = rw::Mul(w->t[r][0], Fl(mem, 1018, 0));
            w->t[r][1] = rw::Mul(w->t[r][1], Fl(mem, 1018, 1));
        }
        w->t[3][2] = rw::Mul(w->t[3][2], Fl(mem, 1018, 2));
        for (int l = 0; l < 3; ++l) w->o[l] = Fl(mem, 1019, l);
        w->o[3] = 0.0f;
    } else {
        for (int r = 0; r < 4; ++r) std::memcpy(w->t[r], vu.regs.vf[28 + r], 16);
        std::memcpy(w->o, vu.regs.vf[2], 16);
    }
    std::memcpy(w->regs, w->t, sizeof w->t);
    if (img.mirror) {
        // the prologue: t' = t + k m1 + k m1 (two roundings), m1' = -m1 (x, y, w); the epilogue restores the
        // registers through 0 + v (a -0 or denormal x, y comes back +0)
        const float k = Fl(mem, 1009, 1);
        for (int l : {0, 1, 3}) {
            float acc = rw::Add(w->t[3][l], 0.0f);
            acc = rw::Madd(acc, w->t[1][l], k);
            w->t[3][l] = rw::Madd(acc, w->t[1][l], k);
            w->t[1][l] = rw::Sub(0.0f, w->t[1][l]);
        }
        for (int r : {1, 3})
            for (int l : {0, 1}) w->regs[r][l] = rw::Add(0.0f, w->regs[r][l]);
    }
    // (a transform with no w, or not finite: every place at w 0 - the VU's saturations, not the GPU's - the microcode's)
    bool finite = false;
    for (int r = 0; r < 4; ++r) {
        if (w->t[r][3] != 0.0f) finite = true;
        for (int l = 0; l < 4; ++l)
            if (!std::isfinite(w->t[r][l])) return -16;
    }
    if (!finite) return -16;
    float* h = w->header;
    std::memset(h, 0, sizeof w->header);
    u32 mode = rw::kRwWorld | (img.cull ? rw::kRwWCull : 0u) | (w->path_b ? rw::kRwWGuard : 0u);
    if (img.lit) mode |= Lo(mem, 1013, 2) != 0 ? rw::kRwWAlphaZero : rw::kRwWLit;
    SetMode(h, mode);
    const u32 first = kNormals;
    std::memcpy(h + 4 * rw::kRwMisc + 3, &first, 4);
    for (int r = 0; r < 4; ++r) std::memcpy(h + 4 * (rw::kRwWT + r), w->t[r], 16);
    std::memcpy(h + 4 * rw::kRwWO, w->o, 16);
    std::memcpy(h + 4 * rw::kRwWScale, mem + 1018 * 4, 2 * 16);    // 1018-1019
    std::memcpy(h + 4 * rw::kRwWG0, mem + 1022 * 4, 2 * 16);       // 1022-1023
    std::memcpy(h + 4 * rw::kRwWL, mem + 1007 * 4, 3 * 16);        // 1007-1009
    const u32 base = ((Lo(mem, 1013, 2) + 0x7FE0) | 0x7FE0) & 0xFFFF;
    std::memcpy(h + 4 * rw::kRwWBase, &base, 4);
    if (img.lit) {
        // the lighting's constants: L by 0.000124, e by 0.01, and K0 = itof0(bits(2s)) - e itof0(bits(1.0))
        const float i1 = rw::F(0x3C23D70Au), i2 = rw::F(0x39020610u);
        float* k = h + 4 * rw::kRwWK;
        for (int l = 0; l < 3; ++l) k[l] = rw::Mul(Fl(mem, 1007, l), i2);
        k[3] = rw::Mul(Fl(mem, 1007, 3), i1);
        const float s = Fl(mem, 1008, 3);
        h[4 * rw::kRwWBase + 1] = rw::Sub(rw::Itof0(rw::B(rw::Add(s, s))), rw::Mul(Fl(mem, 1007, 3), rw::Itof0(0x3F800000u)));
    }
    const u32* in = mem + w->top * 4;
    w->indices.clear();
    for (u32 t = 0; t + 2 < w->n; ++t) {
        if (in[12 * (t + 2) + 3] & 0x8000) continue;
        w->indices.push_back(3 * t);
        w->indices.push_back(3 * t + 1);
        w->indices.push_back(3 * t + 2);
    }
    return 1;
}

// Path B's CLIP as the microcode leaves it: each vertex's judgement after what the run began with, and one more
// (the loop's pipelining: the quadwords past the batch, as memory has them - with the VU's own arithmetic).
u32 WorldClipEnd(const Vu& vu, const WorldImage& img, const World& w) {
    u32 clip = vu.clip & 0xFFFFFF;
    if (!w.path_b) return clip;
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    const u32* in = mem + w.top * 4;
    const u32 judged = img.cull ? (w.n + 1) & ~1u : w.n;     // (2678's and 1332's loops: two vertices an iteration)
    const u32 from = judged > 4 ? judged - 4 : 0;
    clip = (clip << (6 * std::min(judged, 4u))) & 0xFFFFFF;
    for (u32 v = from; v < judged; ++v) {
        const u32 j = v < w.n ? rw::WorldJudge(w.header, in, v) : rw::WorldJudgeVu(w.header, mem + ((w.top + 3 * v) & 0x3FF) * 4);
        clip |= j << (6 * (judged - 1 - v));
    }
    return clip & 0xFFFFFF;
}

// ---------------------------------------------------------------- the world's path C: fog and the six planes' clipper
//
// (spec_2678EB26.md 3, build/native/model_2678_1332/pc.py: the model this follows step for step.) The batch's
// places H = (x, y, w, w) and their judgements (the frustum's: 1020-1021) decide it: all out by one plane -
// nothing; none out - the fast path (every vertex, the GPU's); else triangle by triangle, the CLIP register's
// last judgements choosing: skipped (its ADC: the flag or, 2678's, the homogeneous winding), unclipped (the
// GPU's: a native draw of the triangles in a row) or clipped against the planes (its polygon made here, as the
// microcode makes it, kicked as a packet of its own between the native draws: the order the microcode's).
// The packets' vertex count (57; 1332's 46) flushes as the microcode's: CLIP's extra judgement, the restart.

struct PathCEvent {
    bool polygon;                       // true: GS words in `words` (a packet's vertices); false: native triangles
    std::vector<u32> words;             // (polygon) ST, RGBAQ, XYZF2 quadwords
    std::vector<u32> indices;           // (native) corners 3t + c
};

struct PathC {
    int kind;                           // 0 culled, 1 fast, 2 clipped
    std::vector<PathCEvent> pool;       // (reused: the first `used` this batch's events)
    u32 used = 0;
    PathCEvent& Next(bool polygon) {
        if (used == pool.size()) pool.emplace_back();
        PathCEvent& e = pool[used++];
        e.polygon = polygon;
        e.words.clear(), e.indices.clear();
        return e;
    }
    struct Range {
        PathCEvent* b;
        PathCEvent* e;
        PathCEvent* begin() const { return b; }
        PathCEvent* end() const { return e; }
    };
    Range events() { return Range{pool.data(), pool.data() + used}; }
    Range events() const { return Range{const_cast<PathCEvent*>(pool.data()), const_cast<PathCEvent*>(pool.data()) + used}; }
    u32 kicks;                          // the microcode's kicks (its buffer swaps)
    u32 clip_end;
    std::vector<u32> written;           // what the first pass leaves in place: per vertex H (4 words), q1.w
};

struct Hc {
    float h[4];
    float wt[4];
};

struct ClipRegister {
    u32 r;
    void Push(u32 j) { r = ((r << 6) | j) & 0xFFFFFF; }
    bool Cand(u32 m) const { return (r & m) != 0; }
    bool Cor(u32 m) const { return ((r | m) & 0xFFFFFF) == 0xFFFFFF; }
};

inline float MinVu(float a, float b) {
    auto order = [](float f) { const u32 b = rw::B(f); const s32 m = s32(b & 0x7FFFFFFFu); return (b >> 31) ? -m : m; };
    return order(b) < order(a) ? b : a;
}

// The path C of a batch as the microcode runs it. `backface`: 2678's (and 1332's) ADC from the winding; `most`:
// the packets' vertex count; `header`: the World header (its transform, 1332's alpha) for the vertices' colours.
void DecidePathC(const Vu& vu, const World& w, bool backface, u32 most, PathC* pc) {
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    const u32* in = mem + w.top * 4;
    const u32 n = w.n, m = (n + 1) & ~1u;
    const float* c18 = reinterpret_cast<const float*>(mem + 1018 * 4);
    const float* c19 = reinterpret_cast<const float*>(mem + 1019 * 4);
    const float* c20 = reinterpret_cast<const float*>(mem + 1020 * 4);
    const float* c21 = reinterpret_cast<const float*>(mem + 1021 * 4);
    const u32 vi07 = ((Lo(mem, 1013, 2) + 0x7FE0) | 0x7FE0) & 0xFFFF;
    const float v25[4] = {c20[1], c21[1], c21[2], 1.0f};
    auto scaled = [&](const float* h) {
        return rw::Judge(rw::Mul(h[0], v25[0]), rw::Mul(h[1], v25[1]), rw::Madd(c20[2], v25[2], h[3]), rw::Mul(h[3], 1.0f));
    };
    static thread_local std::vector<Hc> H;
    static thread_local std::vector<u32> J, adc;
    H.resize(m), J.resize(m), adc.assign(m, 0);
    u32 all = 0x3F, any = 0;
    for (u32 i = 0; i < m; ++i) {
        const u32* q0 = in + 12 * i;
        float c[4];
        if (i < n) {
            const float p[3] = {rw::F(q0[0]), rw::F(q0[1]), rw::F(q0[2])};
            rw::WorldClip(w.header, p, c);
        } else {
            // (the stale vertex past an odd batch: its place with the VU's own arithmetic)
            using namespace lifted;
            const float* t0 = w.t[0], *t1 = w.t[1], *t2 = w.t[2], *t3 = w.t[3];
            for (int l : {0, 1, 3}) {
                u32 acc = BinaryOf<kMul>(rw::B(t3[l]), kOne, 0);
                acc = BinaryOf<kMadd>(rw::B(t0[l]), q0[0], acc);
                acc = BinaryOf<kMadd>(rw::B(t1[l]), q0[1], acc);
                c[l] = rw::F(BinaryOf<kMadd>(rw::B(t2[l]), q0[2], acc));
            }
        }
        float* h = H[i].h;
        h[0] = c[0], h[1] = c[1], h[2] = c[3], h[3] = c[3];
        J[i] = scaled(h);
        all &= J[i], any |= J[i];
    }
    if (backface) {
        auto cross = [](const float* a, const float* b, float* r) {
            const float A[3] = {a[1], a[2], a[0]}, Bv[3] = {b[2], b[0], b[1]}, Bb[3] = {b[1], b[2], b[0]}, Aa[3] = {a[2], a[0], a[1]};
            for (int l = 0; l < 3; ++l) r[l] = rw::Sub(rw::Mul(A[l], Bv[l]), rw::Mul(Bb[l], Aa[l]));
        };
        auto dot3 = [](const float* c, const float* h) {
            return rw::Add(rw::Add(rw::Mul(c[2], h[2]), rw::Mul(c[0], h[0])), rw::Mul(1.0f, rw::Mul(c[1], h[1])));
        };
        for (u32 i = 2; i < m; ++i) {
            float c[3], det;
            u32 base;
            if (i % 2 == 0) {
                cross(H[i].h, H[i - 1].h, c);
                det = dot3(c, H[i - 2].h);
                base = vi07;
            } else {
                cross(H[i - 1].h, H[i - 2].h, c);
                det = dot3(c, H[i].h);
                base = vi07 + 0x8000;
            }
            adc[i] = ((base + ((rw::B(det) >> 31) ? 0x20u : 0u)) & 0x8000u) | (in[12 * i + 3] & 0xFFFF);
        }
        adc[0] = in[3] & 0xFFFF, adc[1] = m > 1 ? in[15] & 0xFFFF : 0;
    } else {
        for (u32 i = 0; i < m; ++i) adc[i] = in[12 * i + 3] & 0xFFFF;
    }
    pc->used = 0;
    pc->kicks = 0;
    pc->written.resize(std::size_t(5) * m);
    for (u32 i = 0; i < m; ++i) {
        std::memcpy(pc->written.data() + 5 * i, H[i].h, 16);
        pc->written[5 * i + 4] = i < 2 ? 0u : adc[i];
    }
    ClipRegister clip{vu.clip & 0xFFFFFF};
    for (u32 i = 0; i < m; ++i) clip.Push(J[i]);
    if (all) {
        pc->kind = 0;
        pc->clip_end = clip.r;
        return;
    }
    if (any == 0) {
        pc->kind = 1;
        pc->kicks = 1;
        pc->clip_end = clip.r;
        PathCEvent& e = pc->Next(false);
        for (u32 t = 0; t + 2 < n; ++t)
            if (!(adc[t + 2] & 0x8000) && !(in[12 * (t + 2) + 3] & 0x8000)) {
                // (the fast path's ADC: fog + itof4 of the flag word - bit 15 where the word has 0x8000)
                e.indices.push_back(3 * t), e.indices.push_back(3 * t + 1), e.indices.push_back(3 * t + 2);
            }
        return;
    }
    pc->kind = 2;
    auto fog = [&](float W) {
        float f = rw::Add(rw::Sub(0.0f, rw::Mul(c19[3], c18[3])), rw::Mul(W, c18[3]));
        return MinVu(rw::Max(f, 0.0f), 255.0f);
    };
    (void)fog;
    PathCEvent* native = nullptr;
    auto triangle = [&](u32 c) {
        if (!native) native = &pc->Next(false);
        const u32 t = c - 2;
        native->indices.push_back(3 * t), native->indices.push_back(3 * t + 1), native->indices.push_back(3 * t + 2);
    };
    PathCEvent* polygons = nullptr;
    static thread_local std::vector<u32> flushes;
    flushes.clear();
    u32 cnt = 0, k = 2;
    bool restart = true;
    clip.Push(J[0]), clip.Push(J[1]), clip.Push(J[2]);
    const float one = 1.0f;
    static thread_local std::vector<Hc> poly, next;
    while (true) {
        const u32 a = k - 2, b = k - 1, c = k;
        if (adc[c] != 0) {
            restart = true;
        } else if (!clip.Cand(0x3FFFF)) {
            triangle(c);
            cnt += restart ? 3 : 1;
            restart = false;
        } else if (clip.Cor(0xFDF7DF)) {
            restart = true;
        } else {
            poly.clear();
            poly.push_back(Hc{{H[b].h[0], H[b].h[1], H[b].h[2], H[b].h[3]}, {0, 0, one, 0}});
            poly.push_back(Hc{{H[c].h[0], H[c].h[1], H[c].h[2], H[c].h[3]}, {0, 0, 0, one}});
            poly.push_back(Hc{{H[a].h[0], H[a].h[1], H[a].h[2], H[a].h[3]}, {0, one, 0, 0}});
            bool dead = false;
            if (clip.Cand(0x020820)) {
                clip.Push(0);
                next.clear();
                Hc prev = poly.back();
                for (const Hc& cur : poly) {
                    const float dp = rw::Sub(prev.h[3], c21[3]), dc = rw::Sub(cur.h[3], c21[3]);
                    const float q = rw::Recip(rw::Sub(cur.h[3], prev.h[3]));
                    const bool po = rw::B(dp) >> 31, co = rw::B(dc) >> 31;
                    if (!(po && co)) {
                        if (po != co) {
                            Hc I;
                            for (int l = 0; l < 4; ++l) {
                                I.h[l] = rw::Mul(rw::Msub(rw::Mul(prev.h[l], dc), cur.h[l], dp), q);
                                I.wt[l] = rw::Mul(rw::Msub(rw::Mul(prev.wt[l], dc), cur.wt[l], dp), q);
                            }
                            I.h[2] = 0.0f;
                            next.push_back(I);
                            clip.Push(rw::Judge(rw::Mul(I.h[0], v25[0]), rw::Mul(I.h[1], v25[1]), rw::Sub(0.0f, c21[3]),
                                                rw::Mul(I.h[3], 1.0f)));
                        }
                        if (!co) {
                            next.push_back(cur);
                            clip.Push(scaled(cur.h));
                        }
                    }
                    prev = cur;
                }
                poly.swap(next);
                if (poly.empty()) dead = true;
            }
            static const u32 kAny[5] = {0x7DF7DF, 0x3CF3CF, 0x1C71C7, 0x0C30C3, 0x041041};
            static const u32 kRej[5] = {0xBEFBEF, 0xDF7DF7, 0xEFBEFB, 0xF7DF7D, 0xFBEFBE};
            static const u32 kPlane[5] = {0x410410, 0x208208, 0x104104, 0x082082, 0x041041};
            for (int pl = 0; pl < 5 && !dead; ++pl) {
                if (!clip.Cand(kAny[pl])) break;
                if (clip.Cor(kRej[pl])) {
                    dead = true;
                    break;
                }
                if (!clip.Cand(kPlane[pl])) continue;
                auto dist = [&](const float* h) {
                    switch (pl) {
                        case 1: return rw::Madd(h[3], c21[1], h[1]);
                        case 2: return rw::Msub(h[3], c21[1], h[1]);
                        case 3: return rw::Madd(h[3], c20[1], h[0]);
                        default: return rw::Msub(h[3], c20[1], h[0]);
                    }
                };
                next.clear();
                Hc prev = poly.back();
                for (const Hc& cur : poly) {
                    float dp, dc, den;
                    if (pl == 0) {
                        dp = rw::Add(rw::Sub(0.0f, prev.h[3]), c20[3]);
                        dc = rw::Add(rw::Sub(0.0f, cur.h[3]), c20[3]);
                        den = rw::Sub(rw::Sub(0.0f, cur.h[3]), rw::Sub(0.0f, prev.h[3]));
                    } else {
                        dp = dist(prev.h), dc = dist(cur.h);
                        den = rw::Sub(dc, dp);
                    }
                    const float q = rw::Recip(den);
                    const bool po = rw::B(dp) >> 31, co = rw::B(dc) >> 31;
                    if (!po) next.push_back(prev);
                    if (po != co) {
                        Hc I;
                        for (int l = 0; l < 4; ++l) {
                            I.h[l] = rw::Mul(rw::Msub(rw::Mul(prev.h[l], dc), cur.h[l], dp), q);
                            I.wt[l] = rw::Mul(rw::Msub(rw::Mul(prev.wt[l], dc), cur.wt[l], dp), q);
                        }
                        next.push_back(I);
                    }
                    prev = cur;
                }
                poly.swap(next);
                if (poly.empty()) dead = true;
            }
            if (!dead) {
                // the polygon as a zig-zag strip: p0, p(n-1), p1, p(n-2), ... (ADC on the first two)
                if (!polygons || native) {
                    polygons = &pc->Next(true);
                    native = nullptr;
                }
                const u32* corner[3] = {in + 12 * a, in + 12 * b, in + 12 * c};
                float uv[3][2], col[3][4];
                for (int j = 0; j < 3; ++j) {
                    uv[j][0] = rw::Itof12(corner[j][4]), uv[j][1] = rw::Itof12(corner[j][5]);
                    u32 rgba[4];
                    std::memcpy(rgba, corner[j] + 8, 16);
                    if (rw::Mode(w.header) & (rw::kRwWLit | rw::kRwWAlphaZero))
                        rgba[3] = rw::WorldAlpha(w.header, in, j == 0 ? a : j == 1 ? b : c);
                    for (int l = 0; l < 4; ++l) col[j][l] = rw::Itof0(rgba[l]);
                }
                const float* c02 = w.o;
                const float c31z = w.t[3][2];
                u32 lo = 0, hi = u32(poly.size()) - 1;
                for (u32 e = 0; e < poly.size(); ++e) {
                    const Hc& p = (e % 2 == 0) ? poly[lo++] : poly[hi--];
                    const float q = rw::Recip(p.h[3]);
                    float f = rw::Sub(rw::Mul(p.h[3], c18[3]), rw::Mul(c19[3], c18[3]));
                    f = rw::Max(MinVu(f, 255.0f), 0.0f);
                    float st[2];
                    for (int l = 0; l < 2; ++l) {
                        float acc = rw::Mul(uv[0][l], p.wt[1]);
                        acc = rw::Madd(acc, uv[1][l], p.wt[2]);
                        st[l] = rw::Madd(acc, uv[2][l], p.wt[3]);
                    }
                    u32 qw[12];
                    qw[0] = rw::B(rw::Mul(st[0], q)), qw[1] = rw::B(rw::Mul(st[1], q)), qw[2] = rw::B(rw::Mul(1.0f, q)), qw[3] = 0;
                    for (int l = 0; l < 4; ++l) {
                        float acc = rw::Mul(col[0][l], p.wt[1]);
                        acc = rw::Madd(acc, col[1][l], p.wt[2]);
                        qw[4 + l] = rw::Ftoi(rw::Madd(acc, col[2][l], p.wt[3]));
                    }
                    qw[8] = rw::Ftoi(rw::Madd(c02[0], p.h[0], q) * 16.0f);
                    qw[9] = rw::Ftoi(rw::Madd(c02[1], p.h[1], q) * 16.0f);
                    qw[10] = rw::Ftoi(rw::Madd(c02[2], c31z, q) * 16.0f);
                    qw[11] = (rw::Ftoi(f * 16.0f) & 0xFFFF) | (e < 2 ? 0x8000u : 0u);
                    polygons->words.insert(polygons->words.end(), qw, qw + 12);
                }
                cnt += u32(poly.size());
            }
            restart = true;
        }
        ++k;
        if (k >= n) {
            // (the loop's tail judges the next triangle's before its end test - after a last flush, its first alone;
            // the vertex past the batch as memory has it: the first pass's place past an odd batch, else raw)
            clip.Push(J[k - 2]);
            if (cnt >= most) break;
            clip.Push(J[k - 1]);
            if (k < m) {
                clip.Push(J[k]);
            } else {
                using namespace lifted;
                // (memory past a full batch may be an output buffer's tag: its words 1-3 the first pass's, 1006's)
                const u32 at = (w.top + 3 * k) & 0x3FF;
                u32 q[4];
                std::memcpy(q, mem + at * 4, 16);
                if (at == (vu.regs.vc[13] & 0x3FF) || at == (vu.regs.vc[14] & 0x3FF))
                    q[1] = mem[1006 * 4 + 1], q[2] = mem[1006 * 4 + 2], q[3] = mem[1006 * 4 + 3];
                // (and word 0 the count of a packet flushed from it: the flushes kick vi14, vi13, vi14, ...)
                for (std::size_t f = 0; f < flushes.size(); ++f)
                    if (at == ((f % 2 == 0 ? vu.regs.vc[14] : vu.regs.vc[13]) & 0x3FF)) q[0] = 0x8000u | flushes[f];
                const u32 g[4] = {BinaryOf<kMul>(q[0], rw::B(v25[0]), 0), BinaryOf<kMul>(q[1], rw::B(v25[1]), 0),
                                  BinaryOf<kMadd>(rw::B(v25[2]), q[3], rw::B(c20[2])), BinaryOf<kMul>(q[3], kOne, 0)};
                clip.Push(rw::JudgeVu(g));
            }
            break;
        }
        bool flushed = false;
        if (cnt >= most) {
            flushes.push_back(cnt);
            ++pc->kicks;
            cnt = 0;
            restart = true;
            flushed = true;
        }
        if (flushed) clip.Push(J[k - 2]);
        clip.Push(J[k - 2]), clip.Push(J[k - 1]), clip.Push(J[k]);
    }
    if (cnt >= 3) ++pc->kicks;
    pc->clip_end = clip.r;
}

// A packet stream's triangles (PACKED ST, RGBAQ, XYZF2 a vertex, tags with NLOOP), as the GS draws a strip: a
// vertex without ADC with two before it in its packet ends one. Each triangle: its three vertices' 10 words.
void StripTriangles(const u32* words, std::size_t count, std::vector<std::array<u32, 33>>* out) {
    std::size_t at = 0;
    while (at + 4 <= count) {
        const u32 nloop = words[at] & 0x7FFF;
        at += 4;
        u32 seen = 0;
        std::array<u32, 11> v[3];
        for (u32 i = 0; i < nloop && at + 12 <= count; ++i, at += 12) {
            const u32* x = words + at;
            v[0] = v[1], v[1] = v[2];
            v[2] = {x[0], x[1], x[2], x[4] & 0xFF, x[5] & 0xFF, x[6] & 0xFF, x[7] & 0xFF, x[8] & 0xFFFF, x[9] & 0xFFFF,
                    (x[10] >> 4) & 0xFFFFFF, (x[11] >> 4) & 0xFF};
            ++seen;
            if (seen >= 3 && !((x[11] >> 15) & 1)) {
                std::array<u32, 33> t;
                for (int j = 0; j < 3; ++j) std::copy(v[j].begin(), v[j].end(), t.begin() + 11 * j);
                out->push_back(t);
            }
        }
    }
}

// The events' triangles, as StripTriangles has them.
void EventTriangles(const World& w, const u32* in, const PathC& pc, std::vector<std::array<u32, 33>>* out) {
    float header[rw::kRwWHeaderQuads * 4];
    std::memcpy(header, w.header, sizeof header);
    SetMode(header, rw::Mode(w.header) | rw::kRwWFog);
    for (const PathCEvent& e : pc.events()) {
        if (e.polygon) {
            std::vector<u32> packet = {0x8000u | u32(e.words.size() / 12), 0, 0, 0};
            packet.insert(packet.end(), e.words.begin(), e.words.end());
            StripTriangles(packet.data(), packet.size(), out);
            continue;
        }
        for (std::size_t i = 0; i + 2 < e.indices.size(); i += 3) {
            std::array<u32, 33> t;
            for (int j = 0; j < 3; ++j) {
                rw::GsVertex o;
                rw::WorldVertex(header, in, e.indices[i + j] / 3 + e.indices[i + j] % 3, &o);
                const u32 x[11] = {o.stq[0], o.stq[1], o.stq[2], o.rgba[0] & 0xFF, o.rgba[1] & 0xFF, o.rgba[2] & 0xFF,
                                   o.rgba[3] & 0xFF, o.xyz[0] & 0xFFFF, o.xyz[1] & 0xFFFF, (o.xyz[2] >> 4) & 0xFFFFFF, o.fog};
                std::copy(x, x + 11, t.begin() + 11 * j);
            }
            out->push_back(t);
        }
    }
}

void SendPathC(Vu& vu, const World& w, const PathC& pc, u64 key, bool upload, bool release, u64 prim) {
    u32* mem = reinterpret_cast<u32*>(vu.data.data());
    const u32* inputs = mem + w.top * 4;
    bool first = true;
    float header[rw::kRwWHeaderQuads * 4];
    std::memcpy(header, w.header, sizeof header);
    static const bool no_fog = std::getenv("PS2HOST_RW_TEST_NOFOG") != nullptr;
    SetMode(header, (rw::Mode(w.header) & ~(rw::kRwWCull | rw::kRwWGuard)) | (no_fog ? 0u : rw::kRwWFog));  // (the CPU's triangles)
    for (const PathCEvent& e : pc.events()) {
        if (!e.polygon) {
            if (e.indices.empty()) continue;
            u32 flags = 0x100 | 0x200 | 4;
            if (first && upload) flags |= 1 | (release ? 2u : 0u);
            first = false;
            SendWorld(e.indices.data(), u32(e.indices.size()), header, flags, key, prim, inputs, w.quads);
            continue;
        }
        // its polygons, as packets of the microcode's (at most 57 vertices each) in the output buffer, kicked
        const u32 b = vu.regs.vc[14] & 0x3FF;
        const u32 room = std::min<u32>(57, (1024 - b - 1) / 3);
        for (std::size_t at = 0; at < e.words.size();) {
            u32 vertices = u32(std::min<std::size_t>(room, (e.words.size() - at) / 12));
            // (a packet ends at a polygon's end: its last vertex without ADC, the next one's first - with ADC)
            auto adc = [&](std::size_t v) { return ((e.words[at + 12 * v + 11] >> 15) & 1) != 0; };
            while (vertices > 0 && at + 12 * std::size_t(vertices) < e.words.size() &&
                   (!adc(vertices) || adc(vertices - 1)))
                --vertices;
            if (vertices < 3) break;
            u32* p = mem + b * 4;
            p[0] = 0x8000u | vertices, p[1] = mem[1006 * 4 + 1], p[2] = mem[1006 * 4 + 2], p[3] = mem[1006 * 4 + 3];
            std::memcpy(p + 4, e.words.data() + at, std::size_t(vertices) * 48);
            ps2host::VuNativeAccess::Kick(vu, b);
            at += 12 * std::size_t(vertices);
        }
    }
    if (first && upload) {
        // (no native draw took the upload: the mesh is not sent; the next batch with these vertices sends it)
        TheState().uploaded.erase(key);
    }
}

template <int I>
void CheckWorld(Vu& vu, u32 pc, u64* pairs) {
    const WorldImage& img = kWorldImages[I];
    static thread_local std::vector<u32> before;
    u32* mem = reinterpret_cast<u32*>(vu.data.data());
    World w;
    const int decided = DecideWorld(vu, img, &w);
    const u32 clip_end = decided > 0 && !w.path_c ? WorldClipEnd(vu, img, w) : 0;
    PathC pc_decided;
    if (decided > 0 && w.path_c) DecidePathC(vu, w, img.cull, img.packet, &pc_decided);
    before.assign(mem, mem + 4096);
    std::vector<u32> kicked;
    auto saved_block = vu.on_kick_block;
    auto saved = vu.on_kick;
    if (saved_block)
        vu.on_kick_block = [&](const u32* x, u32 c) { kicked.insert(kicked.end(), x, x + c); saved_block(x, c); };
    else
        vu.on_kick = [&](u64 lo, u64 hi) {
            const u32 q[4] = {u32(lo), u32(lo >> 32), u32(hi), u32(hi >> 32)};
            kicked.insert(kicked.end(), q, q + 4);
            if (saved) saved(lo, hi);
        };
    *pairs = vu.RunEmulated(pc, u64(1) << 40);
    vu.on_kick_block = saved_block;
    vu.on_kick = saved;
    if (decided < 0) return;
    CheckStats& s = TheCheck();
    ++s.runs;
    if (w.path_c) {
        std::vector<std::array<u32, 33>> theirs, mine;
        StripTriangles(kicked.data(), kicked.size(), &theirs);
        EventTriangles(w, before.data() + w.top * 4, pc_decided, &mine);
        u32 packets = 0;
        for (std::size_t at = 0; at + 4 <= kicked.size(); at += 4 + 12 * std::size_t(kicked[at] & 0x7FFF)) ++packets;
        if (packets != pc_decided.kicks) ++s.regs_differ;
        if ((vu.clip & 0xFFFFFF) != pc_decided.clip_end && s.clip_differ++ < 6) {
            std::fprintf(stderr, "[rw native] path C check (%s): run %llu CLIP microcode %06X twin %06X (n %u, kind %d, top %X, vi13 %X vi14 %X)\n", img.name,
                         (unsigned long long)s.runs, vu.clip & 0xFFFFFF, pc_decided.clip_end, w.n, pc_decided.kind, w.top,
                         vu.regs.vc[13] & 0xFFFF, vu.regs.vc[14] & 0xFFFF);
            for (u32 d = 0; d < 3; ++d) {
                const u32 qa = (w.top + 3 * (w.n + d)) & 0x3FF;
                std::fprintf(stderr, "[rw native]   quad %X before %08X %08X %08X %08X, after %08X %08X %08X %08X\n", qa,
                             before[qa * 4], before[qa * 4 + 1], before[qa * 4 + 2], before[qa * 4 + 3], mem[qa * 4],
                             mem[qa * 4 + 1], mem[qa * 4 + 2], mem[qa * 4 + 3]);
            }
        }
        s.words += 33 * theirs.size();
        if (theirs.size() != mine.size()) {
            ++s.differ;
            if (s.told++ < 12)
                std::fprintf(stderr, "[rw native] path C check (%s): run %llu %zu triangles, the twin %zu (n %u, kind %d)\n", img.name,
                             (unsigned long long)s.runs, theirs.size(), mine.size(), w.n, pc_decided.kind);
            return;
        }
        for (std::size_t i = 0; i < theirs.size(); ++i)
            for (int x = 0; x < 33; ++x)
                if (theirs[i][x] != mine[i][x]) {
                    ++s.differ;
                    if (s.told++ < 12)
                        std::fprintf(stderr, "[rw native] path C check (%s): run %llu triangle %zu word %d: microcode %08X, twin %08X (kind %d)\n",
                                     img.name, (unsigned long long)s.runs, i, x, theirs[i][x], mine[i][x], pc_decided.kind);
                }
        if (s.runs % 20000 == 0) SayCheck();
        return;
    }
    if (kicked.size() != std::size_t(4) * (1 + 3 * w.n)) {
        if (s.told++ < 12)
            std::fprintf(stderr, "[rw native] world check (%s): run %llu kicked %zu words, the twin %u\n", img.name,
                         (unsigned long long)s.runs, kicked.size(), 4 * (1 + 3 * w.n));
        ++s.differ;
        return;
    }
    if ((vu.clip & 0xFFFFFF) != clip_end && s.clip_differ++ < 6)
        std::fprintf(stderr, "[rw native] world check: CLIP microcode %06X twin %06X (n %u, path %c, image %d)\n", vu.clip & 0xFFFFFF,
                     clip_end, w.n, w.path_b ? 'B' : 'A', I);
    if (w.rebuild || img.mirror) {
        for (int r = 0; r < 4; ++r)
            if (std::memcmp(w.regs[r], vu.regs.vf[28 + r], 16) != 0) ++s.regs_differ;
        if (std::memcmp(w.o, vu.regs.vf[2], 16) != 0) ++s.regs_differ;
    }
    const u32* in = before.data() + w.top * 4;
    for (u32 v = 0; v < w.n; ++v) {
        rw::GsVertex o;
        rw::WorldVertex(w.header, in, v, &o);
        const u32* x = kicked.data() + 4 + 12 * v;
        const u32 mine[10] = {o.stq[0], o.stq[1], o.stq[2], o.rgba[0], o.rgba[1], o.rgba[2], o.rgba[3],
                              o.xyz[0], o.xyz[1], o.xyz[2]};
        const u32 theirs[10] = {x[0], x[1], x[2], x[4], x[5], x[6], x[7], x[8], x[9], x[10]};
        for (int i = 0; i < 10; ++i) {
            ++s.words;
            if (mine[i] != theirs[i]) {
                ++s.differ;
                if (s.told++ < 12)
                    std::fprintf(stderr, "[rw native] world check (%s): run %llu vertex %u word %d: twin %08X, microcode %08X (path %c)\n",
                                 img.name, (unsigned long long)s.runs, v, i, mine[i], theirs[i], w.path_b ? 'B' : 'A');
            }
        }
        if (v >= 2) {
            const u32 drawn = rw::WorldDrawn(w.header, in, v) ? 0u : 1u;
            if (((x[11] >> 15) & 1) != drawn && s.adc_differ++ < 6) {
                float a[2], b[2], c[2];
                rw::WorldScreen(w.header, in, v - 2, a);
                rw::WorldScreen(w.header, in, v - 1, b);
                rw::WorldScreen(w.header, in, v, c);
                std::fprintf(stderr, "[rw native] world check (%s): run %llu vertex %u of %u ADC: microcode %u (w %08X), twin %u; mode %X base %X, 1013 %X %X %X %X; S %g,%g %g,%g %g,%g; flags %X %X %X\n",
                             img.name, (unsigned long long)s.runs, v, w.n, (x[11] >> 15) & 1, x[11], drawn,
                             rw::Mode(w.header), rw::B(w.header[4 * rw::kRwWBase]), before[1013 * 4], before[1013 * 4 + 1],
                             before[1013 * 4 + 2], before[1013 * 4 + 3], a[0], a[1], b[0], b[1], c[0], c[1],
                             in[12 * (v - 2) + 3], in[12 * (v - 1) + 3], in[12 * v + 3]);
                if (s.adc_differ == 1)
                    for (int r = 0; r < 6; ++r)
                        std::fprintf(stderr, "[rw native]   header %d: %08X %08X %08X %08X; mem %u: %08X %08X %08X %08X\n", r,
                                     rw::B(w.header[4 * (1 + r)]), rw::B(w.header[4 * (1 + r) + 1]), rw::B(w.header[4 * (1 + r) + 2]),
                                     rw::B(w.header[4 * (1 + r) + 3]), 1014 + r, before[(1014 + r) * 4], before[(1014 + r) * 4 + 1],
                                     before[(1014 + r) * 4 + 2], before[(1014 + r) * 4 + 3]);
            }
        }
    }
    if (s.runs % 20000 == 0) SayCheck();
}

template <int I>
bool NativeWorld(Vu& vu, u32 entry, u64* pairs) {
    const WorldImage& img = kWorldImages[I];
    static thread_local World w;
    if (const int why = DecideWorld(vu, img, &w); why < 0) {
        Declined("world", why);
        return false;
    }
    u32* mem = reinterpret_cast<u32*>(vu.data.data());
    // PS2HOST_RW_TEST_SHADOW=1 (a test): path C's draws native, but VU1 left as the microcode leaves it (run silently)
    static const bool shadow = std::getenv("PS2HOST_RW_TEST_SHADOW") != nullptr;
    static thread_local std::vector<u8> saved_mem;
    static thread_local lifted::Context saved_regs;
    u32 saved_clip = 0;
    if (shadow && w.path_c) {
        saved_mem.assign(vu.data.data(), vu.data.data() + 16384);
        saved_regs = vu.regs;
        saved_clip = vu.clip;
    }
    // 1332's lighting: its alphas into the batch, in place, as the microcode's pre-pass writes them (for the groups
    // of four) - the mesh then has them (PS2HOST_RW_GPU_ALPHA=1: the shader's own, a test)
    static const bool gpu_alpha = std::getenv("PS2HOST_RW_GPU_ALPHA") != nullptr;
    if (img.lit && !gpu_alpha) {
        const u32* in = mem + w.top * 4;
        static thread_local std::vector<u32> alpha;
        alpha.resize(w.n4);
        for (u32 v = 0; v < w.n4; ++v) alpha[v] = rw::WorldAlpha(w.header, in, v);
        for (u32 v = 0; v < w.n4; ++v) mem[((w.top + 3 * v + 2) & 0x3FF) * 4 + 3] = alpha[v];
        SetMode(w.header, rw::Mode(w.header) & ~(rw::kRwWLit | rw::kRwWAlphaZero));
    }
    static thread_local PathC pc;
    if (w.path_c) {
        DecidePathC(vu, w, img.cull, img.packet, &pc);
        // PS2HOST_NATIVE_RW_PATHC=fast (a test): only the batches all inside (the fast path) natively; =0 none
        static const char* only = std::getenv("PS2HOST_NATIVE_RW_PATHC");
        static const char* image = std::getenv("PS2HOST_RW_TEST_IMAGE");
        if ((image && std::atoi(image) != I) ||
            (only && (std::string(only) == "0" || (std::string(only) == "fast" && pc.kind == 2)))) {
            Declined("world", -12);
            return false;
        }
    }
    const u32 clip_end = w.path_c ? pc.clip_end : WorldClipEnd(vu, img, w);
    // what the run leaves: 1013.w 0, the transform and 1013.y (rebuilt), the buffers swapped (the packet: vi14's)
    mem[1013 * 4 + 3] &= 0xFFFF0000u;
    if (w.rebuild || img.mirror) {
        for (int r = 0; r < 4; ++r) std::memcpy(vu.regs.vf[28 + r], w.regs[r], 16);
        std::memcpy(vu.regs.vf[2], w.o, 16);
    }
    if (w.rebuild) mem[1013 * 4 + 1] = (mem[1013 * 4 + 1] & 0xFFFF0000u) | w.vi12;
    if (img.mirror) {
        // (what the prologue saves: 1011.w, 1012.w, 1014.z-1017.z; vf10.z-vf13.z the same; vf01.xyw 0)
        std::memcpy(mem + 1011 * 4 + 3, &w.regs[1][3], 4);
        std::memcpy(mem + 1012 * 4 + 3, &w.regs[3][3], 4);
        const float saved[4] = {w.regs[1][0], w.regs[1][1], w.regs[3][0], w.regs[3][1]};
        for (int i = 0; i < 4; ++i) {
            std::memcpy(mem + (1014 + i) * 4 + 2, &saved[i], 4);
            std::memcpy(&vu.regs.vf[10 + i][2], &saved[i], 4);
        }
        vu.regs.vf[1][0] = vu.regs.vf[1][1] = vu.regs.vf[1][3] = 0;
    }
    const u32* inputs = mem + w.top * 4;
    State& st = TheState();
    const u64 key = Hash(0x2545F4914F6CDD1Dull ^ (u64(I) << 56) ^ w.n, inputs, std::size_t(4) * w.quads) | (u64(3) << 62);
    u32 flags = 0x100 | 0x200 | 4;
    if (!st.uploaded.count(key)) {
        if (st.uploaded.size() >= 20000 || st.uploaded_quads > (u64(16) << 20) / 16) {
            flags |= 2;
            st.uploaded.clear();
            st.uploaded_quads = 0;
        }
        flags |= 1;
        st.uploaded.insert(key);
        st.uploaded_quads += w.quads;
    }
    const u32 tag1 = mem[1006 * 4 + 1];
    if (w.path_c) {
        SendPathC(vu, w, pc, key, (flags & 1) != 0, (flags & 2) != 0, (tag1 >> 15) & 0x7FF);
        if (shadow) {
            std::memcpy(vu.data.data(), saved_mem.data(), 16384);
            vu.regs = saved_regs;
            ps2host::VuNativeAccess::SetClip(vu, saved_clip);
            auto kb = vu.on_kick_block;
            auto k = vu.on_kick;
            vu.on_kick_block = [](const u32*, u32) {};
            vu.on_kick = nullptr;
            *pairs = vu.RunEmulated(entry, u64(1) << 40);
            vu.on_kick_block = kb;
            vu.on_kick = k;
            return true;
        }
        // what the microcode leaves in memory: the batch transformed in place (q0 H, q1.w the ADC words), the tag's
        // words 1-3 in both output buffers
        const u32 m = (w.n + 1) & ~1u;
        for (u32 i = 0; i < m; ++i) {
            u32* q0 = mem + ((w.top + 3 * i) & 0x3FF) * 4;
            std::memcpy(q0, pc.written.data() + 5 * i, 16);
            mem[((w.top + 3 * i + 1) & 0x3FF) * 4 + 3] = pc.written[5 * i + 4];
        }
        for (u32 b : {vu.regs.vc[13] & 0x3FF, vu.regs.vc[14] & 0x3FF})
            mem[b * 4 + 1] = mem[1006 * 4 + 1], mem[b * 4 + 2] = mem[1006 * 4 + 2], mem[b * 4 + 3] = mem[1006 * 4 + 3];
        if (pc.kicks & 1) std::swap(vu.regs.vc[13], vu.regs.vc[14]);
    } else {
        SendWorld(w.indices.data(), u32(w.indices.size()), w.header, flags, key, (tag1 >> 15) & 0x7FF, inputs, w.quads);
        std::swap(vu.regs.vc[13], vu.regs.vc[14]);
    }
    ps2host::VuNativeAccess::SetClip(vu, clip_end);
    vu.regs.vc[15] = 4;
    vu.resume = img.stub;
    *pairs = 60 + 12 * u64(w.n);
    ++st.runs;
    return true;
}

template <int I>
bool RunWorld(Vu& vu, u32 pc, u64* pairs) {
    const WorldImage& img = kWorldImages[I];
    if (pc != 0x0020 && pc != img.stub) {
        if (pc != 0) Declined("world", -7);
        return false;
    }
    if (pc == img.stub && (vu.regs.vc[15] & 0xFFFF) != 4) {
        Declined("world", -8);
        return false;
    }
    if (TheMode() == Mode::kCheck) {
        CheckWorld<I>(vu, pc, pairs);
        return true;
    }
    return NativeWorld<I>(vu, pc, pairs);
}

}  // namespace

void RegisterVuNativeRw() {
    ps2host::RegisterVuNative({"RenderWare's cars (Burnout 3, VU1 7E86891B)", Holds, Run});
    ps2host::RegisterVuNative({"RenderWare's instanced strips (Burnout 3, VU1 D4B61373)", HoldsInstanced, RunInstanced});
    ps2host::RegisterVuNative({kWorldImages[0].name, HoldsWorld<0>, RunWorld<0>});
    ps2host::RegisterVuNative({kWorldImages[1].name, HoldsWorld<1>, RunWorld<1>});
    ps2host::RegisterVuNative({kWorldImages[2].name, HoldsWorld<2>, RunWorld<2>});
    ps2host::RegisterVuNative({kWorldImages[3].name, HoldsWorld<3>, RunWorld<3>});
    std::atexit([] {
        if (TheMode() == Mode::kCheck) SayCheck();
    });
}
