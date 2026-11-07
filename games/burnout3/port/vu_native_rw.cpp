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

// What a run of the cars' program decides from VU1's state as it starts.
struct Cars {
    u32 top, n, n4;
    bool rebuild, path_a, guard, prefix;
    float c[4][4], o[4];                // the transform (vf28-vf31) and vf02 - rebuilt, or as the registers hold them
    u32 vi12;
    float header[rw::kRwHeaderQuads * 4];
    std::vector<u32> indices;           // the strip's triangles without ADC
    std::vector<u32> adc;               // per vertex: XYZF2's bit 15 as the microcode leaves it
    u32 clip_end;                       // the CLIP flag after the run
};

// -1: not this way (the microcode's).
int Decide(const Vu& vu, Cars* k) {
    const u32* mem = reinterpret_cast<const u32*>(vu.data.data());
    if (Lo(mem, 1013, 2) != 0) return -1;                  // (the skinning)
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
            const float p[3] = {rw::F(q0[0]), rw::F(q0[1]), rw::F(q0[2])};
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
        const float p[3] = {rw::F(q[0]), rw::F(q[1]), rw::F(q[2])};
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
        SetMode(k.header, (stream ? rw::kRwEnvStream : 0u) | (k.guard && k.path_a ? rw::kRwGuardPlace : 0u));
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

u64 Hash(u64 h, const u32* p, size_t words) { return ps2host::VuNativeHash(p, words * 4, h); }

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

bool Native(Vu& vu, u64* pairs) {
    Cars k;
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
    SetMode(k.header, t4 ? rw::kRwGuardPlace : 0u);
    Send(k, k.header, flags, key, 0x5C, inputs);
    if (k.path_a) {
        SetMode(k.header, rw::kRwEnvStream | (t4 ? rw::kRwGuardPlace : 0u));
        Send(k, k.header, 0x100 | 0x200 | 4, key, 0x25C, inputs);
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
    static u64 counts[2][16] = {}, all = 0;
    const int which = program[0] == 'c' ? 0 : 1;
    ++counts[which][std::min(15, -reason)];
    if (say && ++all % 10000 == 0) {
        std::fprintf(stderr, "[rw native] declined:");
        for (int w = 0; w < 2; ++w)
            for (int r = 0; r < 16; ++r)
                if (counts[w][r]) std::fprintf(stderr, " %s %d: %llu", w ? "instanced" : "cars", -r, (unsigned long long)counts[w][r]);
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
    Batch b;
    if (const int why = DecideInstanced(vu, &b); why < 0) {
        Declined("instanced", why);
        mem[932 * 4 + 3] = w932;
        return false;
    }
    const u32* inputs = mem + b.top * 4;
    State& st = TheState();
    const u64 key = Hash(0x165667B19E3779F9ull ^ b.n, inputs, std::size_t(12) * b.n4) | (u64(3) << 62);
    u32 vi13 = vu.regs.vc[13] & 0xFFFF, vi14 = vu.regs.vc[14] & 0xFFFF;
    Cars shape;                                 // (SendSized's: the batch's size, an instance's indices)
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

}  // namespace

void RegisterVuNativeRw() {
    ps2host::RegisterVuNative({"RenderWare's cars (Burnout 3, VU1 7E86891B)", Holds, Run});
    ps2host::RegisterVuNative({"RenderWare's instanced strips (Burnout 3, VU1 D4B61373)", HoldsInstanced, RunInstanced});
    std::atexit([] {
        if (TheMode() == Mode::kCheck) SayCheck();
    });
}
