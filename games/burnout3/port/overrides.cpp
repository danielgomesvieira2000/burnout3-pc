/*
 * Burnout 3's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently (the GS, the
 * IOP's services) and the way enhancements hook in. Register them here with
 * ps2host::Override(address, function, name); see engine/docs/PORTING.md.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "controls.h"
#include "gpu_pipelines.h"
#include "ps2host/host.h"
#include "ps2host/iop_vm.h"
#include "ps2host/memory.h"
#include "ps2host/mc2.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// prompts.cpp: whether the controller's prompts show (the controller used last).
bool ControllerPrompts();
// dev_menu.cpp: the game's development menu opened, and back from it.
void OpenDevMenu(lifted::Context& c);
bool DevMenuReturned();

// libmc2's handlers as the game has them, for when no card is given.
void F_00244218(lifted::Context& c);
void F_002445D0(lifted::Context& c);
void F_00246588(lifted::Context& c);
void F_00246788(lifted::Context& c);
void F_00245FC0(lifted::Context& c);
void F_002470D8(lifted::Context& c);
void F_00245398(lifted::Context& c);
// A screen with a movie: its update each frame (a method; see SkipMovie).
void F_001313C0(lifted::Context& c);
// The race's loading, and the game's (boot, after a race), a step each
// frame (see FastLoading).
void F_0019A950(lifted::Context& c);
// RenderWare's wait for its packet buffers to flip (see AddIdleLoop below).
void F_001F2408(lifted::Context& c);
void F_00228040(lifted::Context& c);
void F_002AF340(lifted::Context& c);
void F_002AEFC0(lifted::Context& c);
void F_003FB0F0(lifted::Context& c);
void F_001322B0(lifted::Context& c);
// The frontend: its string lookup, the main menu's input, a message box.
void F_00271A00(lifted::Context& c);
void F_00318100(lifted::Context& c);
void F_003181E0(lifted::Context& c);
void F_00321470(lifted::Context& c);
void F_0031C350(lifted::Context& c);
// A menu's selector: Up and Down (see SelectorMove).
void F_00319360(lifted::Context& c);
void F_00319330(lifted::Context& c);

bool LanEnabled();                 // iop_servers.cpp: the LAN, on unless B3_LAN=0
void LanPresetLogin();             // lan.cpp: SELECT EA ACCOUNT logs in by itself
void LanStopLobby();               // lan_lobby.cpp: the lobby this game served, closed

// The graphics settings' defaults (graphics.cpp).
const char* GraphicsDefault(const char* key);
const char* GraphicsPresetValue(const char* key, bool modern);
int ParticlesLevel();
bool OrbitCameraOn();                  // orbit_camera.cpp

namespace {

/* 0x001156C0, the SDK's kernel-mode store: it disables interrupts, enters kernel
   mode, stores $a1 at $a0 and returns through `eret` with interrupts restored. The
   timer-register setters (0x00115730 and its two neighbours) tail-call it. The
   host has no modes, so the store is all there is to it. */
void KernelStore(lifted::Context& c) {
    generated::Write32(u32(c.r[4]), u32(c.r[5]));
}

/* 0x0010D3E8, the SDK's TTY write over DECI2 (buffer $a0, length $a1): what the
   game prints with printf. A retail console has no debugger to send it to; the
   port logs it, since it is the game telling what it is doing. */
void TtyWrite(lifted::Context& c) {
    const u32 buffer = u32(c.r[4]), length = u32(c.r[5]);
    std::string text;
    for (u32 i = 0; i < length && i < 4096; ++i) text += char(generated::ReadU8(buffer + i));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (!text.empty()) std::fprintf(stderr, "[tty] %s\n", text.c_str());
    c.r[2] = length;
}

/* libmc2's command handlers (FINDINGS: "Memory cards"): with --memcard the
   host's card answers them (ps2host/mc2.h); without one the game's own run,
   and find no card, as a console with an empty slot. The worker dispatches
   commands through 0x004BB320 (indexed by the command less 2); the game
   issues 2 GetInfo, 3 Format, 5 ReadFile, 6 WriteFile, 7 and 11 (Create, for
   files and directories), 10 GetDir and 14 SearchFile, which the worker
   makes of the lookup 0x00245398 itself. */
template <void (*Host)(lifted::Context&), void (*Game)(lifted::Context&)>
void CardOr(lifted::Context& c) {
    if (ps2host::mc2::CardDirectory().empty()) Game(c);
    else Host(c);
}

/* 0x001313C0, the update of a screen that plays a movie (FINDINGS:
   "Skipping movies"): in state 0x17 (+0x2F8) it drives the movie at +0x1C8
   (0x00325030 decodes and shows the next picture). The movie's +0x48 is set
   while it plays, +0x49 once it has finished, and +0x4A asks it to stop -
   0x00324ED0, the game's own request, which the update then honours: it
   cancels the stream's read in flight, marks the movie finished, and the
   screen goes on as at the movie's end. A press of Start (a press, not a
   hold) asks it here, for every movie the game sits through - not one that
   loops (+0x4B: the stream starts again at its end), which plays behind a
   screen waiting for input. */
void SkipMovie(lifted::Context& c) {
    const u32 screen = u32(c.r[4]), movie = screen + 0x1C8;
    static u16 before = 0;
    const u16 held = ps2host::PadButtonsHeld();
    const bool start = (held & 8u) && !(before & 8u);
    before = held;
    const bool playing = generated::ReadU32(screen + 0x2F8) == 0x17 && generated::ReadU8(movie + 0x48) &&
                         !generated::ReadU8(movie + 0x49) && !generated::ReadU8(movie + 0x4A);
    // +0x4B: it loops (the stream goes back to its start at the end) - the
    // title screen's background, which Start is for, not to be skipped.
    const bool loops = generated::ReadU8(movie + 0x4B) != 0;
    static const bool log = std::getenv("PS2HOST_MOVIE_LOG") != nullptr;
    if (log && playing) {
        static std::set<std::tuple<u32, u32, u32>> seen;
        const u32 vtable = generated::ReadU32(screen), which = generated::ReadU32(screen + 0x2FC);
        if (seen.insert({screen, vtable, which}).second)
            std::fprintf(stderr, "[movie] screen %08X class %08X movie %u: +48..4F %02X %02X %02X %02X %02X %02X %02X %02X\n",
                         screen, vtable, which, unsigned(generated::ReadU8(movie + 0x48)),
                         unsigned(generated::ReadU8(movie + 0x49)), unsigned(generated::ReadU8(movie + 0x4A)),
                         unsigned(generated::ReadU8(movie + 0x4B)), unsigned(generated::ReadU8(movie + 0x4C)),
                         unsigned(generated::ReadU8(movie + 0x4D)), unsigned(generated::ReadU8(movie + 0x4E)),
                         unsigned(generated::ReadU8(movie + 0x4F)));
    }
    if (playing && start && !loops) {
        if (log) std::fprintf(stderr, "[movie] skipped: screen %08X movie %u\n", screen, generated::ReadU32(screen + 0x2FC));
        generated::Write8(movie + 0x4A, 1);
    }
    F_001313C0(c);
}

/* A function that may wait in an idle loop: in real time the wait sleeps
   (ps2host::InterruptWait). */
/* 0x00228040(waiting, room): the function RenderWare's packet wait (the
   loop at 0x001F24E0) calls each turn - it keeps the largest room seen
   (0x006662E0). Each turn still waiting sleeps until an event first
   (ps2host::WaitForGuestEvent): at 144 pictures a second the loop's turns,
   each a call through the dispatcher, were a third of the EE's time, its
   idle wait only taking over at the next millisecond's look. */
void PacketWaitTurn(lifted::Context& c) {
    if (u32(c.r[4]) != 0) ps2host::WaitForGuestEvent();
    F_00228040(c);
}

/* 0x002AF340: RenderWare Audio's answers from the IOP (SIF user command
   1), in the SIF0 interrupt: each into a ring of 24 (0x01E81C40, 0x30
   bytes each; written gp-0x5A30, read gp-0x5A34) for its receiver thread
   (0x002AF2A0) - one that comes while the ring is full is dropped. */
/* The last of RenderWare Audio's commands to the IOP and its answers, for
   the log when the music's start is given up (MusicStart): what led there. */
struct RwaRecord {
    u64 frame, last = 0, repeats = 0;   // the same again since, to `last` (a poll a frame kept the ring full)
    bool answer;
    u32 words[6];
};
std::array<RwaRecord, 1024> g_rwa_records{};
size_t g_rwa_next = 0;
std::map<u32, u64> g_rwa_answer_types;      // answers by type (+0x10), all of them
std::map<u32, u64> g_rwa_command_types;     // commands by type, all of them

void Record(bool answer, const u32 (&words)[6]) {
    if (g_rwa_next) {
        RwaRecord& last = g_rwa_records[(g_rwa_next - 1) % g_rwa_records.size()];
        if (last.answer == answer && std::memcmp(last.words, words, sizeof words) == 0) {
            ++last.repeats;
            last.last = ps2host::FrameNumber();
            return;
        }
    }
    RwaRecord& r = g_rwa_records[g_rwa_next++ % g_rwa_records.size()];
    r.frame = ps2host::FrameNumber();
    r.last = 0;
    r.repeats = 0;
    r.answer = answer;
    std::memcpy(r.words, words, sizeof words);
}

/* 0x002AEFC0(a0, a1, a2, type, t0..t3): RenderWare Audio's command to the
   IOP (a record of user command 0). */
void RwaCommand(lifted::Context& c) {
    const u32 words[6] = {u32(c.r[7]), u32(c.r[4]), u32(c.r[5]), u32(c.r[6]), u32(c.r[8]), u32(c.r[9])};
    // The status poll (0x14, every frame, its answer between) only counted:
    // kept, it and its answer filled the ring in eight seconds.
    ++g_rwa_command_types[words[0]];
    if (words[0] != 0x14) Record(false, words);
    F_002AEFC0(c);
}

void RwaAnswer(lifted::Context& c) {
    const u32 packet = u32(c.r[4]);
    u32 words[6];
    for (int k = 0; k < 6; ++k) words[k] = u32(generated::ReadU32(packet + 0x10u + 4u * u32(k)));
    // Every one (the same again is counted, not kept: Record) - a stream's
    // asking for data (0x47) among them.
    ++g_rwa_answer_types[words[0]];
    if (words[0] != 0x14 && words[0] != 0x0D) Record(true, words);  // (the poll's and the mixer's: counted)
    const u32 gp = u32(c.r[28]);
    const s32 written = s32(generated::ReadS32(gp - 0x5A30u)), read = s32(generated::ReadS32(gp - 0x5A34u));
    if (written - read >= 24) {
        static unsigned dropped = 0;
        ++dropped;
        std::fprintf(stderr, "[rwa] an answer from the IOP dropped (the ring of 24 full): %u so far, frame %llu\n",
                     dropped, static_cast<unsigned long long>(ps2host::FrameNumber()));
    }
    F_002AF340(c);
}

void SayAudio();

/* The EE's 32 MB and the IOP's 2 MB of memory, as they are, to two files
   beside the log (for looking into a hang afterwards). */
void SaveMemories(const char* ee, const char* iop) {
    ps2host::Memory& m = ps2host::GuestMemory();
    if (std::FILE* f = std::fopen(ee, "wb")) {
        std::fwrite(m.rdram(), 1, ps2host::kRdramSize, f);
        std::fclose(f);
    }
    if (std::FILE* f = std::fopen(iop, "wb")) {
        std::fwrite(m.Translate(ps2host::kIopRamBase, ps2host::kIopRamSize), 1, ps2host::kIopRamSize, f);
        std::fclose(f);
    }
}

/* 0x003FB0F0(music, 1): the EA Trax player's step - from the frontend's
   load (0x00131480, phase 10, returning to 0x001316D0) until it says the
   music has started. Its stream (RenderWare Audio's) has been seen to wait
   for ever for the last track's to stop (the IOP's state 4, after a
   finished race went on to the next event, at 120 pictures a second) - the
   loading screen with it. Given up after 3 s: the load goes on, without the
   music for now, and what RenderWare Audio said last goes to the log. */
void MusicStart(lifted::Context& c) {
    // The loads that wait for it: the frontend's (0x00131480, phase 10) and a
    // race's - its world (0x0019A950, phase 4) waits for the game's sound
    // manager (0x00289EE0, 0x01E77540, state 24), which waits for this
    // (seen 2026-10-22: the infinite loading screen again, from here).
    // Any other caller too (the screen after a race, a car unlocked, waited
    // so as well), while the stream is stuck stopping (state 4).
    static std::map<u32, u64> waiting_since;
    const u32 from = u32(c.r[31]);
    const u32 music = u32(c.r[4]);
    F_003FB0F0(c);
    if (u32(c.r[2]) != 0) {
        waiting_since.erase(from);
        return;
    }
    const u32 handle = u32(generated::ReadU32(music + 0xC0u));
    const u32 stream = handle ? u32(generated::ReadU32(handle + 0x1CCu)) : 0;
    const bool stuck = stream && generated::ReadU32(stream + 0x24Cu) == 4;
    if (from != 0x001316D0u && from != 0x00289FE4u && !stuck) {
        waiting_since.erase(from);
        return;
    }
    const u64 now = ps2host::ClockNs();
    u64& since = waiting_since[from];
    if (!since) since = now;
    if (now - since < 3'000'000'000ull) return;
    since = 0;
    c.r[2] = 1;
    std::fprintf(stderr, "[music] the EA Trax stream did not start in 3 s (waited from 0x%08X): the game goes on "
                 "without it.\n", from);
    SayAudio();
    // The first time: the threads and both memories as it gives up
    // (music_ee.bin, music_iop.bin beside the log) - what the stream waits on.
    static bool saved = false;
    if (!saved) {
        saved = true;
        std::fprintf(stderr, "%s%s", ps2host::ThreadReport().c_str(), ps2host::iop::Report().c_str());
        SaveMemories("music_ee.bin", "music_iop.bin");
    }
    std::fflush(stderr);
}

/* The EA Trax player's state and what RenderWare Audio said last (> its
   commands, < the IOP's answers), to the log. */
void SayAudio() {
    const u32 music = 0x01E7A888u, handle = u32(generated::ReadU32(music + 0xC0u));
    const u32 stream = handle ? u32(generated::ReadU32(handle + 0x1CCu)) : 0;
    std::fprintf(stderr, "  EA Trax player %u/%u, its stream's state %u; RenderWare Audio's last commands (>) and "
                 "answers (<):\n",
                 unsigned(generated::ReadU32(music + 0x170u)), unsigned(generated::ReadU32(music + 0xC8u)),
                 stream ? unsigned(generated::ReadU32(stream + 0x24Cu)) : 0u);
    for (size_t k = 0; k < g_rwa_records.size(); ++k) {
        const RwaRecord& r = g_rwa_records[(g_rwa_next + k) % g_rwa_records.size()];
        if (!r.frame) continue;
        std::fprintf(stderr, "  %8llu %c %08X %08X %08X %08X %08X %08X", static_cast<unsigned long long>(r.frame),
                     r.answer ? '<' : '>', r.words[0], r.words[1], r.words[2], r.words[3], r.words[4], r.words[5]);
        if (r.repeats)
            std::fprintf(stderr, "  (and %llu times more, to frame %llu)", static_cast<unsigned long long>(r.repeats),
                         static_cast<unsigned long long>(r.last));
        std::fprintf(stderr, "\n");
    }
    for (const auto& [type, count] : g_rwa_answer_types)
        std::fprintf(stderr, "  answers of type %08X: %llu\n", type, static_cast<unsigned long long>(count));
    for (const auto& [type, count] : g_rwa_command_types)
        std::fprintf(stderr, "  commands of type %08X: %llu\n", type, static_cast<unsigned long long>(count));
    std::fprintf(stderr, "%s", ps2host::iop::SoundIrqTrace().c_str());
}

template <void (*Wait)(lifted::Context&)>
void WaitsForInterrupt(lifted::Context& c) {
    ps2host::InterruptWait waiting;
    Wait(c);
}

/* The main loop's (0x00132600) loading states, each a function it calls
   once a frame until it returns non-zero (FINDINGS: "Loading a race"):
   0x0019A950 in state 4, a race's world, a phase a frame; 0x001322B0 in
   states 7 and 9, the game's own - the long load at boot, and after a race.
   A step only asks whether what it waits for is done (the IOP's reads and
   streams), so taken once a frame the loading waits mostly on frames going
   by. In real time the step is taken again within the frame, the IOP
   running a slice between steps, for up to half a frame: the vertical
   blanks - and the loading screen's spinner, the music - keep their pace.
   A race's load 6.3 s -> 2.6 s, the boot's 5.9 s -> 2.4 s (what is left is
   mostly the race's sound streams, which fill as the sound plays).
   PS2HOST_LOAD_SPEED=1: a step a frame, as the console. */
/* A load that has not finished in 20 s (the infinite loading screen, #16,
   seen again after the EA Trax wait's way out): once, to the log, what the
   threads and RenderWare Audio are doing, and the EE's and IOP's memories
   beside it (stuck_ee.bin, stuck_iop.bin) - for finding what it waits on. */
void StuckLoad(u32 load, u32 caller) {
    std::fprintf(stderr, "[load] 0x%08X (from 0x%08X) not done after 20 s, frame %llu: the threads, RenderWare Audio "
                 "and stuck_ee.bin / stuck_iop.bin beside the log\n%s%s",
                 load, caller, static_cast<unsigned long long>(ps2host::FrameNumber()), ps2host::ThreadReport().c_str(),
                 ps2host::iop::Report().c_str());
    SayAudio();
    SaveMemories("stuck_ee.bin", "stuck_iop.bin");
    std::fflush(stderr);
}

/* F12 (a capture): also the threads, RenderWare Audio's last words, the
   SPU2's interrupts (report.txt) and both memories (ee.bin, iop.bin) in its
   folder - any hang looked into from the moment the player saw it. */
void CaptureHang(const std::string& dir) {
    std::fprintf(stderr, "[capture] the threads, the sound and both memories too\n%s%s",
                 ps2host::ThreadReport().c_str(), ps2host::iop::Report().c_str());
    SayAudio();
    std::fflush(stderr);
    SaveMemories((dir + "/ee.bin").c_str(), (dir + "/iop.bin").c_str());
}

template <void (*Load)(lifted::Context&), u32 kAddress>
void FastLoading(lifted::Context& c) {
    static const bool console = [] {
        const char* speed = std::getenv("PS2HOST_LOAD_SPEED");
        return speed && std::atoi(speed) <= 1;
    }();
    static u64 waiting_since = 0;
    static bool said = false;
    constexpr u64 kStepsNs = 8'000'000;         // half a frame
    const u32 caller = u32(c.r[31]);
    u64 args[32];
    std::memcpy(args, c.r, sizeof args);
    const u64 from = ps2host::ClockNs();
    Load(c);
    if (!console)
        while (!u32(c.r[2]) && !ps2host::BlankDue() && ps2host::ClockNs() - from < kStepsNs &&
               ps2host::RunIopBetweenSteps()) {
            std::memcpy(c.r, args, sizeof args);
            Load(c);
        }
    if (u32(c.r[2])) {
        waiting_since = 0;
        said = false;
    } else if (!waiting_since) {
        waiting_since = from;
    } else if (!said && from - waiting_since > 20'000'000'000ull) {
        said = true;
        StuckLoad(kAddress, caller);
    }
}

// ------------------------------------------------------------ PC SETTINGS
//
// The main menu (FINDINGS: "PC settings") gets two entries of the port's:
// its ONLINE (EA's servers are long gone) is PC SETTINGS, a list of the
// port's settings shown in the main menu's own list, and QUIT GAME is added
// after DRIVER DETAILS, asking first in the game's own YES/NO box. The game
// draws every word of it - its strings (0x00271A00, the lookup), its list
// (0x00321470) and ticker (0x0031C350), its message box (0x0032D080).

/* 0x00271A00(holder, id): the string with that id, from the language file
   (DATA/GLOBALUS.BIN, its table at 0x007A4880). Strings of the online flow
   - which nothing can show now - give the port the guest memory its own
   words and lists live in. */
struct Donor { u32 id, chars; };
const Donor kDonors[] = {{108, 122}, {109, 135}, {110, 110}, {182, 99}, {1187, 143}, {1108, 75}, {3161, 82},
                         {2868, 68}, {2870, 68}, {1206, 68}, {112, 71}, {113, 67}, {116, 88}};

/* Hands out guest memory from the donors (they are never shown), in order:
   texts and arrays the menu points at. Made once, when the language file is
   in (the first main menu); the same every time after. */
class GuestPool {
public:
    void Begin(lifted::Context& c) {
        if (ready_) return;
        // The lookup's holder is the main loop's (0x0051BA44): whoever calls
        // here (a list, a ticker) has its own a0.
        u64 saved[32];
        std::memcpy(saved, c.r, sizeof saved);
        for (const Donor& d : kDonors) {
            c.r[4] = 0x0051BA44u;
            c.r[5] = d.id;
            F_00271A00(c);
            if (u32(c.r[2])) regions_.push_back({u32(c.r[2]), 2 * d.chars});
        }
        std::memcpy(c.r, saved, sizeof saved);
        ready_ = true;
    }
    u32 Take(u32 bytes, u32 align = 2) {
        for (auto& [at, left] : regions_) {
            const u32 start = (at + align - 1) & ~(align - 1);
            if (start + bytes <= at + left) {
                left -= start + bytes - at;
                at = start + bytes;
                return start;
            }
        }
        return 0;
    }
private:
    bool ready_ = false;
    std::vector<std::pair<u32, u32>> regions_;      // (next free, bytes left)
};
GuestPool g_pool;

/* A guest UTF-16 string the port writes (room for `chars`). */
struct GuestText {
    u32 at = 0, chars = 0;
    void Make(u32 room) { chars = room; at = g_pool.Take(2 * (room + 1)); }
    void Set(const std::string& text) {
        if (!at) return;
        u32 i = 0;
        for (; i < text.size() && i < chars; ++i) generated::Write16(at + 2 * i, u8(text[i]));
        generated::Write16(at + 2 * i, 0);
    }
};

// The main menu's entries, by its own string ids (0x00318340 builds them).
constexpr u32 kMainMenuOnline = 0xB8;       // "ONLINE"
constexpr u32 kOnlineHelp = 0xEC;           // "PLAY BURNOUT 3 ONLINE", its description
constexpr u32 kEntries = 6;                 // the game's five, and QUIT GAME
constexpr u32 kMultiplayerEntry = 2, kPcSettingsEntry = 3, kQuitEntry = 5;

/* PC SETTINGS' pages: first VIDEO, GRAPHICS and CONTROLS, Cross on one
   showing its rows - settings, each a few values, Cross going to the next
   (ON/OFF, or the internal resolution's three); CONTROLS' first four rows
   open the bindings' pages. Triangle goes back a page, as on the game's own
   screens (the legend says so): no BACK row. The list shows six rows (it is
   set up with them; a seventh shifts the main menu up): a page with more
   scrolls, Up past the top row or Down past the bottom one bringing the
   next into view. */
enum class Page { kMainMenu, kPcSettings, kVideo, kGraphics, kControls, kMods, kOnline, kBindings, kMultiplayer, kPlayers };
struct PageInfo {
    Page page;
    const char* name;
    const char* help;
};
const PageInfo kPages[] = {
    {Page::kVideo, "VIDEO", "THE WINDOW, FRAME RATE, RESOLUTION, SHAPE AND SMOOTHING"},
    {Page::kGraphics, "GRAPHICS", "THE GAME'S OWN EFFECTS"},
    {Page::kControls, "CONTROLS", "YOUR KEYS AND BUTTONS, AND THE BUTTONS SHOWN"},
    {Page::kMods, "MODS", "NEHALEM'S MODS: KM/H, MIRROR MODE, TRAFFIC AND MORE"},
    {Page::kOnline, "ONLINE", "WHAT YOUR DISCORD PROFILE SHOWS OF THE GAME"},
};
constexpr u32 kPageCount = sizeof kPages / sizeof kPages[0];

struct Option {
    Page page;
    const char* name;
    const char* help;
    int (*now)();                           // the value's index
    void (*set)(int);
    const char* values[8];
    int count;
};
int Fullscreen() { return ps2host::WindowFullscreen() ? 1 : 0; }
void SetFullscreen(int on) { ps2host::SetWindowFullscreen(on != 0); }
int Vsync() { return ps2host::WindowVsync() ? 1 : 0; }
void SetVsync(int on) { ps2host::SetWindowVsync(on != 0); }
int Vrr() { return ps2host::WindowVrr() ? 1 : 0; }
void SetVrr(int on) { ps2host::SetWindowVrr(on != 0); }
int GpuBoost() { return ps2host::WindowGpuBoost() ? 1 : 0; }
void SetGpuBoost(int on) { ps2host::SetWindowGpuBoost(on != 0); }
/* A setting's value among `values` (its index; the default's, `fallback`,
   if it is none of them - unset, the default). */
template <std::size_t N>
int Among(const char* key, const char* const (&values)[N], int fallback = 0) {
    const std::string now = ps2host::Setting(key, values[fallback]);
    for (std::size_t i = 0; i < N; ++i)
        if (now == values[i]) return int(i);
    return fallback;
}
// FRAME RATE (high_fps.cpp): a one-player race's pictures, its steps at 60.
const char* const kFrameRates[] = {"60", "120", "display"};
int FrameRate() { return Among("frame_rate", kFrameRates); }
void SetFrameRate(int which) {
    ps2host::SetSetting("frame_rate", kFrameRates[which]);
    // DISPLAY: fullscreen at the screen's own rate (144 kept), the others at
    // one 60 divides evenly (120 on a 144 Hz screen: the menus even).
    ps2host::SetWindowKeepRefresh(std::string(kFrameRates[which]) == "display");
}
const char* const kResolutions[] = {"original", "2x", "display"};
int Resolution() { return Among("resolution", kResolutions); }
void SetResolution(int which) { ps2host::SetSetting("resolution", kResolutions[which]); }
// GRAPHICS (graphics.cpp): each a setting, ORIGINAL as the game draws it,
// MODERN the PC's (the defaults: GraphicsDefault).
template <std::size_t N>
int AmongGraphics(const char* key, const char* const (&values)[N]) {
    const std::string now = ps2host::Setting(key, GraphicsDefault(key));
    for (std::size_t i = 0; i < N; ++i)
        if (now == values[i]) return int(i);
    return 0;
}
const char* const kBlooms[] = {"original", "off", "modern", "modern_low"};
int Bloom() { return AmongGraphics("bloom", kBlooms); }
void SetBloom(int which) { ps2host::SetSetting("bloom", kBlooms[which]); }
const char* const kBlurs[] = {"original", "off", "modern", "modern_low"};
int Blur() { return AmongGraphics("blur", kBlurs); }
void SetBlur(int which) { ps2host::SetSetting("blur", kBlurs[which]); }
const char* const kShadows[] = {"original", "modern", "soft", "off"};
int Shadows() { return AmongGraphics("shadows", kShadows); }
void SetShadows(int which) { ps2host::SetSetting("shadows", kShadows[which]); }
const char* const kOcclusions[] = {"off", "on"};
int Occlusion() { return AmongGraphics("ao", kOcclusions); }
void SetOcclusion(int which) { ps2host::SetSetting("ao", kOcclusions[which]); }
const char* const kModerns[] = {"original", "modern"};
int Lighting() { return AmongGraphics("lighting", kModerns); }
void SetLighting(int which) { ps2host::SetSetting("lighting", kModerns[which]); }
const char* const kReflectionModes[] = {"original", "modern", "ssr_low", "ssr_high"};
int Reflections() {
    if (ps2host::Setting("reflections", "") == "ssr") return 2;    // (saved before LOW and HIGH)
    return AmongGraphics("reflections", kReflectionModes);
}
// TEXTURE FILTERING (graphics.cpp): the game's, MODERN, or the player's filter.
const char* const kTextureFilters[] = {"original", "modern", "nearest", "bilinear",
                                       "trilinear", "aniso4", "aniso8", "aniso16"};
int Textures() { return AmongGraphics("textures", kTextureFilters); }
int Smoke() { return AmongGraphics("smoke", kModerns); }
int Dof() { return AmongGraphics("dof", kModerns); }
void SetDof(int which) { ps2host::SetSetting("dof", kModerns[which]); }
void SetSmoke(int which) { ps2host::SetSetting("smoke", kModerns[which]); }
void SetTextures(int which) { ps2host::SetSetting("textures", kTextureFilters[which]); }
void SetReflections(int which) { ps2host::SetSetting("reflections", kReflectionModes[which]); }
const char* const kLods[] = {"original", "high"};
int Lod() { return AmongGraphics("lod", kLods); }
void SetLod(int which) { ps2host::SetSetting("lod", kLods[which]); }
// PARTICLES: Nehalem's patch, partial (MORE) or full (MOST): two mods'
// switches (port/tools/mods_spec.py).
int Particles() { return ParticlesLevel(); }
void SetParticles(int which) {
    static const char* const kLevels[] = {"original", "more", "most"};
    ps2host::SetSetting("particles", kLevels[which]);
    ps2host::SetSettingOn("mod.particles_more", which == 1);
    ps2host::SetSettingOn("mod.particles_most", which == 2);
}
// PRESET (GRAPHICS' first row): every row at once - ORIGINAL the game's own
// pictures, MODERN the PC's; CUSTOM shown once a row is changed alone (and
// choosing it goes to the other preset).
const char* const kPresetKeys[] = {"lighting", "ao", "bloom", "blur", "shadows", "reflections",
                                   "dof", "smoke", "textures", "sharpen", "lod"};
int Preset() {
    const bool modern = ps2host::Setting("graphics", "modern") != "original";
    for (const char* key : kPresetKeys)
        if (ps2host::Setting(key, GraphicsDefault(key)) != GraphicsPresetValue(key, modern)) return 2;
    if (Particles() != (modern ? 1 : 0)) return 2;
    return modern ? 1 : 0;
}
void SetPreset(int which) {
    if (which == 2) which = Preset() == 1 ? 0 : 1;
    const bool modern = which == 1;
    ps2host::SetSetting("graphics", modern ? "modern" : "original");
    for (const char* key : kPresetKeys) ps2host::SetSetting(key, GraphicsPresetValue(key, modern));
    SetParticles(modern ? 1 : 0);
}
// SHARPENING (the grade's): contrast-adaptive, the fine detail of textures
// at a high resolution kept crisp.
int Sharpen() { return AmongGraphics("sharpen", kModerns); }
void SetSharpen(int which) { ps2host::SetSetting("sharpen", kModerns[which]); }
const char* const kAspects[] = {"4:3", "16:9", "auto"};
int Aspect() { return Among("aspect", kAspects); }
void SetAspect(int which) { ps2host::SetWindowAspect(kAspects[which]); }
const char* const kAntiAliasings[] = {"off", "fxaa", "smaa", "ssaa2"};
int AntiAliasing() { return Among("antialiasing", kAntiAliasings); }
void SetAntiAliasing(int which) { ps2host::SetWindowAntiAliasing(kAntiAliasings[which]); }
const char* const kPromptStyles[] = {"auto", "playstation", "xbox"};
// PERFORMANCE OVERLAY (a phone's; the PC's F1): the frame rate, and each part's load.
int PerfOverlay() { return ps2host::SettingOn("fps_counter", false) ? (ps2host::SettingOn("fps_detail", false) ? 2 : 1) : 0; }
void SetPerfOverlay(int which) {
    ps2host::SetSettingOn("fps_counter", which > 0);
    ps2host::SetSettingOn("fps_detail", which == 2);
}
// TOUCH CONTROLS (a phone's): the on-screen controller, as its button at the top.
int TouchControls() { return ps2host::TouchControlsShown() ? 1 : 0; }
void SetTouchControls(int on) { ps2host::SetTouchControls(on != 0); }
int ButtonPrompts() { return Among("button_prompts", kPromptStyles); }
void SetButtonPrompts(int which) { ps2host::SetSetting("button_prompts", kPromptStyles[which]); }
// TRIGGER EFFECTS (triggers.cpp): "on" the full strength, as before.
const char* const kTriggerEffects[] = {"off", "light", "on"};
int TriggerEffects() { return Among("trigger_effects", kTriggerEffects, 2); }
void SetTriggerEffects(int which) { ps2host::SetSetting("trigger_effects", kTriggerEffects[which]); }
// DISCORD STATUS (presence.cpp): on unless the player turns it off.
int Discord() { return ps2host::SettingOn("discord", true) ? 1 : 0; }
void SetDiscord(int on) { ps2host::SetSettingOn("discord", on != 0); }
// MODS (mods.cpp): each a setting, off unless the player turns it on.
template <const char* kKey>
int Mod() { return ps2host::SettingOn(kKey, false) ? 1 : 0; }
template <const char* kKey>
void SetMod(int on) { ps2host::SetSettingOn(kKey, on != 0); }
constexpr char kModKph[] = "mod.kph", kModMirror[] = "mod.mirror",
               kModNoTraffic[] = "mod.no_traffic", kModCrashBreaker[] = "mod.crash_breaker",
               kModHeadlights[] = "mod.headlights",
               kModVerticalSplit[] = "mod.vertical_split", kModDetachParts[] = "mod.detach_parts";
// ORBIT CAMERA: off unless turned on (orbit_camera.cpp: OrbitCameraOn).
int OrbitCamera() { return OrbitCameraOn() ? 1 : 0; }
void SetOrbitCamera(int on) { ps2host::SetSettingOn("mod.orbit_camera", on != 0); }
// TRAFFIC: OFF (no traffic, from the next race) or a density, 1X the game's
// (mods.cpp: the traffic's spacing).
const char* const kTrafficDensities[] = {"0.5", "1", "1.5", "2", "3"};
int Traffic() {
    if (Mod<kModNoTraffic>()) return 0;
    return 1 + Among("traffic_density", kTrafficDensities, 1);
}
void SetTraffic(int which) {
    SetMod<kModNoTraffic>(which == 0);
    if (which > 0) ps2host::SetSetting("traffic_density", kTrafficDensities[which - 1]);
}
const Option kOptions[] = {
    {Page::kVideo, "FULLSCREEN", "THE WHOLE SCREEN OR A WINDOW (ALT+ENTER)", Fullscreen, SetFullscreen, {"OFF", "ON"}, 2},
    {Page::kVideo, "VERTICAL SYNC", "NO TEARING, OR THE LEAST DELAY", Vsync, SetVsync, {"OFF", "ON"}, 2},
    {Page::kVideo, "VRR (G-SYNC, FREESYNC)", "ON IF YOUR SCREEN'S REFRESH FOLLOWS THE GAME: SMOOTH HIGH FRAME RATES",
     Vrr, SetVrr, {"OFF", "ON"}, 2},
    {Page::kVideo, "GPU BOOST", "THE GPU KEPT CLOCKED UP: NO HITCH AT A NEW SCENE. OFF SAVES BATTERY", GpuBoost, SetGpuBoost,
     {"OFF", "ON"}, 2},
    {Page::kVideo, "FRAME RATE", "RACES AT 60 PICTURES A SECOND, 120, OR THE SCREEN'S RATE", FrameRate, SetFrameRate,
     {"60", "120", "DISPLAY"}, 3},
    {Page::kVideo, "RESOLUTION", "DRAWN AS THE PS2 DID, TWICE AS SHARP, OR AS SHARP AS THE SCREEN", Resolution,
     SetResolution, {"ORIGINAL", "2X", "DISPLAY"}, 3},
    {Page::kVideo, "PERFORMANCE OVERLAY", "THE FRAME RATE IN A CORNER, AND HOW BUSY EACH PART IS", PerfOverlay,
     SetPerfOverlay, {"OFF", "FPS", "FPS + CPU"}, 3},
    {Page::kVideo, "ASPECT RATIO", "THE PICTURE'S SHAPE: A TELEVISION'S, WIDESCREEN, OR THE SCREEN'S OWN", Aspect,
     SetAspect, {"ORIGINAL (4:3)", "WIDESCREEN (16:9)", "AUTO"}, 3},
    {Page::kVideo, "ANTIALIASING", "EDGES AS DRAWN, SMOOTHED (FXAA, SMAA), OR SUPERSAMPLED (SSAA)",
     AntiAliasing, SetAntiAliasing, {"OFF", "FXAA", "SMAA", "SSAA 2X"}, 4},
    {Page::kGraphics, "PRESET", "EVERY ROW AT ONCE: THE GAME AS IT WAS, OR MODERN PICTURES", Preset,
     SetPreset, {"ORIGINAL", "MODERN", "CUSTOM"}, 3},
    {Page::kGraphics, "LIGHTING", "THE PICTURE'S TONE AND COLOUR: AS DRAWN, OR MODERN", Lighting,
     SetLighting, {"ORIGINAL", "MODERN"}, 2},
    {Page::kGraphics, "AMBIENT OCCLUSION", "SHADE WHERE THINGS MEET: CARS AND ROAD, CORNERS", Occlusion,
     SetOcclusion, {"OFF", "ON"}, 2},
    {Page::kGraphics, "BLOOM", "THE GLOW AROUND LIGHTS: AS DRAWN, NONE, OR MODERN - FULL OR SOFTER", Bloom,
     SetBloom, {"ORIGINAL", "OFF", "MODERN HIGH", "MODERN LOW"}, 4},
    {Page::kGraphics, "MOTION BLUR", "THE SPEED'S BLUR: AS DRAWN, NONE, OR THE CAMERA'S - FULL OR SHORTER", Blur,
     SetBlur, {"ORIGINAL", "OFF", "MODERN HIGH", "MODERN LOW"}, 4},
    {Page::kGraphics, "SHADOWS", "THE CARS' SHADOWS: AS DRAWN, THE SUN'S, SOFTER, OR NONE", Shadows, SetShadows,
     {"ORIGINAL", "MODERN", "SOFT", "OFF"}, 4},
    {Page::kGraphics, "REFLECTIONS", "THE CARS' PAINT: AS DRAWN, LACQUERED, OR MIRRORING THE SCENE TOO",
     Reflections, SetReflections, {"ORIGINAL", "MODERN", "SSR LOW", "SSR HIGH"}, 4},
    {Page::kGraphics, "CINEMATIC CAMERAS", "TAKEDOWNS: AS DRAWN, OR WITH DEPTH OF FIELD", Dof, SetDof,
     {"ORIGINAL", "MODERN"}, 2},
    {Page::kGraphics, "SMOKE AND FIRE", "AS DRAWN, OR SOFT WHERE THEY MEET THE ROAD AND THE CARS", Smoke,
     SetSmoke, {"ORIGINAL", "MODERN"}, 2},
    {Page::kGraphics, "TEXTURE FILTERING", "HOW TEXTURES ARE SMOOTHED: AS DRAWN, OR YOUR CHOICE", Textures,
     SetTextures, {"ORIGINAL", "MODERN", "NEAREST", "BILINEAR", "TRILINEAR", "ANISOTROPIC 4X", "ANISOTROPIC 8X",
                   "ANISOTROPIC 16X"}, 8},
    {Page::kGraphics, "SHARPENING", "FINE DETAIL KEPT CRISP: AS DRAWN, OR SHARPENED", Sharpen, SetSharpen,
     {"ORIGINAL", "MODERN"}, 2},
    {Page::kGraphics, "LOD", "THE CARS' DETAILED MODELS FARTHER AWAY", Lod, SetLod, {"ORIGINAL", "HIGH"}, 2},
    {Page::kGraphics, "PARTICLES", "SMOKE AND SPARKS WHILE DRIVING: AS DRAWN, MORE, OR THE MOST", Particles,
     SetParticles, {"ORIGINAL", "MORE", "MOST"}, 3},
    {Page::kMods, "SPEEDOMETER", "THE SPEED IN MILES OR IN KILOMETRES AN HOUR", Mod<kModKph>, SetMod<kModKph>,
     {"MPH", "KM/H"}, 2},
    {Page::kMods, "MIRROR MODE", "EVERY TRACK MIRRORED, LEFT FOR RIGHT", Mod<kModMirror>, SetMod<kModMirror>,
     {"OFF", "ON"}, 2},
    {Page::kMods, "TRAFFIC", "HOW MUCH TRAFFIC THE ROADS HAVE (OFF: FROM THE NEXT RACE)", Traffic, SetTraffic,
     {"OFF", "0.5X", "1X", "1.5X", "2X", "3X"}, 6},
    {Page::kMods, "CRASH BREAKER", "SET OFF IN CRASH MODE ONLY, OR IN EVERY MODE (CONTROLS)",
     Mod<kModCrashBreaker>, SetMod<kModCrashBreaker>, {"CRASH MODE", "ALL MODES"}, 2},
    {Page::kMods, "ORBIT CAMERA", "THE CONTROLLER'S RIGHT STICK TURNS THE CAMERA AROUND YOUR CAR", OrbitCamera,
     SetOrbitCamera, {"OFF", "ON"}, 2},
    {Page::kMods, "ROAD RAGE PARTS", "CRASHED CARS SHED THEIR PARTS IN EVERY MODE, ROAD RAGE TOO",
     Mod<kModDetachParts>, SetMod<kModDetachParts>, {"OFF", "ON"}, 2},
    {Page::kMods, "HEADLIGHTS", "AS THE TRACK HAS THEM, OR EVERY RACE CAR'S ALWAYS ON", Mod<kModHeadlights>,
     SetMod<kModHeadlights>, {"ORIGINAL", "ALWAYS ON"}, 2},
    {Page::kMods, "SPLITSCREEN", "TWO PLAYERS' VIEWS ONE ABOVE THE OTHER, OR SIDE BY SIDE", Mod<kModVerticalSplit>,
     SetMod<kModVerticalSplit>, {"HORIZONTAL", "VERTICAL"}, 2},   // (VERTICAL: parts of the HUD still wrong, FINDINGS)
    {Page::kControls, "TOUCH CONTROLS", "THE ON-SCREEN CONTROLLER: SHOWN, OR HIDDEN FOR A REAL ONE", TouchControls,
     SetTouchControls, {"OFF", "ON"}, 2},
    {Page::kControls, "GYRO STEERING", "TURN THE PHONE LIKE A WHEEL TO STEER; HIGH NEEDS THE LEAST TURN",
     controls::GyroLevel, controls::SetGyroLevel, {"OFF", "LOW", "MEDIUM", "HIGH"}, 4},
    {Page::kControls, "BUTTON PROMPTS", "THE BUTTONS SHOWN: YOUR CONTROLLER'S, PLAYSTATION OR XBOX", ButtonPrompts,
     SetButtonPrompts, {"AUTO", "PLAYSTATION", "XBOX"}, 3},
    {Page::kControls, "TRIGGER EFFECTS", "DUALSENSE: THE TRIGGERS FEEL THE ENGINE, THE BRAKES AND THE GRIP",
     TriggerEffects, SetTriggerEffects, {"OFF", "LIGHT", "ON"}, 3},
    {Page::kOnline, "DISCORD STATUS", "YOUR PROFILE SAYS WHAT YOU PLAY: THE EVENT, THE TRACK, THE TIME",
     Discord, SetDiscord, {"OFF", "ON"}, 2},
};
constexpr u32 kOptionCount = sizeof kOptions / sizeof kOptions[0];

/* CONTROLS' pages of bindings (controls.h): the keyboard's and the
   controller's, racing and in the menus. */
struct BindingsPage {
    controls::Device device;
    controls::Context context;
    const char* name;
    const char* help;
};
const BindingsPage kBindingsPages[] = {
    {controls::Device::kKeyboard, controls::Context::kRacing, "KEYBOARD (RACING)", "THE KEYS YOU RACE WITH"},
    {controls::Device::kKeyboard, controls::Context::kMenus, "KEYBOARD (MENUS)",
     "THE KEYS FOR THE MENUS AND THE PAUSE SCREEN"},
    {controls::Device::kController, controls::Context::kRacing, "CONTROLLER (RACING)", "THE BUTTONS YOU RACE WITH"},
    {controls::Device::kController, controls::Context::kMenus, "CONTROLLER (MENUS)",
     "THE BUTTONS FOR THE MENUS AND THE PAUSE SCREEN"},
};
constexpr int kBindingsPageCount = sizeof kBindingsPages / sizeof kBindingsPages[0];

/* What the platform shows (ps2host::OnPhone, KeyboardPresent): a phone's
   screen is always the whole of it and synced, with no VRR or GPU boost to
   ask for and no Discord; it has its touch controls instead. A keyboard's
   bindings only with a keyboard. */
bool Shown(const Option& o) {
    if (ps2host::OnPhone())
        return o.set != SetFullscreen && o.set != SetVsync && o.set != SetVrr && o.set != SetGpuBoost;
    return o.set != SetTouchControls && o.set != controls::SetGyroLevel && o.set != SetPerfOverlay;  // (a phone's)
}
bool Shown(const PageInfo& p) { return !(ps2host::OnPhone() && p.page == Page::kOnline); }
bool Shown(const BindingsPage& b) {
    return b.device != controls::Device::kKeyboard || ps2host::KeyboardPresent();
}
// The row of PC SETTINGS' link to kPages[page] (kPageCount: DEVELOPER MENU's).
int PageRow(u32 page) {
    int row = 0;
    for (u32 i = 0; i < page && i < kPageCount; ++i) row += Shown(kPages[i]) ? 1 : 0;
    return row;
}
// The row of CONTROLS' link to kBindingsPages[page].
int BindingsRow(int page) {
    int row = 0;
    for (int i = 0; i < page; ++i) row += Shown(kBindingsPages[i]) ? 1 : 0;
    return row;
}

/* MULTIPLAYER (the main menu's) first asks how: SPLITSCREEN - two players
   on this PC - LAN (the game's online flow on the PC's network: lan.cpp) or
   ONLINE (EA's servers are gone: greyed). SPLITSCREEN then seats the
   players: each presses Start (Enter on the keyboard, or as bound) on the
   device they play with, the first player 1, the next player 2; Cross takes
   them to the game's own MULTIPLAYER, a second controller plugged in
   (ps2host::SetPortDevices). In its menus every device - keyboard, mouse,
   controllers - is still player 1's (and drives player 2's screens, its car's
   choice, too); racing, each seat's device drives its own car alone
   (controls.cpp: SetPortsSplit). The main menu again, the seats are gone. */
struct Mode {
    const char* name;
    const char* help;
    bool (*open)();
};
bool Always() { return true; }
bool Never() { return false; }
const Mode kModes[] = {
    {"SPLITSCREEN", "TWO PLAYERS ON THIS DEVICE, THE SCREEN SHARED", Always},
    {"LAN", "RACE PCS AND PHONES ON YOUR NETWORK, OR OVER A VPN", LanEnabled},
    {"ONLINE", "RACE OVER THE INTERNET: NOT YET", Never},
};
constexpr int kSplitscreenMode = 0, kLanMode = 1;
constexpr int kModeCount = sizeof kModes / sizeof kModes[0];
constexpr int kPlayers = 2;

/* A row of a page, and what choosing it does. */
struct Item {
    enum Kind { kPageLink, kOption, kBindingsLink, kAction, kLayout, kReset, kDevMenu, kMode, kPlayer, kPlay } kind;
    int index;                  // kPages', kOptions', kBindingsPages', kModes', a player's or the page's actions' index
};

// The rows' words: the longest a row or a description may be.
constexpr u32 kRowChars = 38, kHelpChars = 70;

/* A question the game's YES/NO box asks for the port. */
enum class Asking { kNone, kQuit, kReset, kLayout };

struct MainMenuState {
    u32 menu = 0;                           // the main menu's object (0x00318340's a0)
    u32 entries = 0, helps = 0;             // our arrays: kEntries entries (8 bytes), help pointers
    u32 game_entries[kEntries][2] = {};     // the main menu's rows, kept while PC SETTINGS shows
    u32 game_helps[kEntries] = {};
    GuestText pc_settings, pc_settings_help, quit, quit_help, question, blank;
    GuestText row_names[kEntries], row_helps[kEntries];
    GuestText hints[controls::kButtonTexts];    // the game's words naming buttons, as bound
    Page page = Page::kMainMenu;            // what the list shows
    int bindings = 0;                       // kBindingsPages' page shown (Page::kBindings)
    std::vector<Item> items;                // the page's rows; the list shows six from `scroll`
    int scroll = 0;
    int capturing = -1;                     // the item a binding is being made for
    int seconds = -1;                       // the time it shows left
    bool made = false;
    Asking asking = Asking::kNone;
    // PLAYERS: the devices seated (ps2host::HostDevices' ids), in the order
    // they pressed Start, and what each device held the frame before.
    std::vector<u32> seated;
    std::map<u32, bool> start_before;
    bool cross_before = true;
};
MainMenuState g_main;

void MakeTexts(lifted::Context& c) {
    if (g_main.made) return;
    g_pool.Begin(c);
    // The largest first, while the donors' room is in one piece.
    for (u32 i = 0; i < kEntries; ++i) g_main.row_helps[i].Make(kHelpChars);
    for (u32 i = 0; i < kEntries; ++i) g_main.row_names[i].Make(kRowChars);
    g_main.entries = g_pool.Take(8 * kEntries, 8);
    g_main.helps = g_pool.Take(4 * kEntries, 4);
    g_main.pc_settings.Make(16);
    g_main.pc_settings.Set(ps2host::OnPhone() ? "PORT SETTINGS" : "PC SETTINGS");
    g_main.pc_settings_help.Make(32);
    g_main.pc_settings_help.Set(ps2host::OnPhone() ? "SET UP THE ANDROID VERSION" : "SET UP THE PC VERSION");
    g_main.quit.Make(16);
    g_main.quit.Set("QUIT GAME");
    g_main.quit_help.Make(32);
    g_main.quit_help.Set("CLOSE BURNOUT 3");
    g_main.question.Make(32);
    g_main.blank.Make(2);
    g_main.blank.Set(" ");               // (an empty string the list does not draw as empty)
    // The game's words naming buttons, when the bound one's name is longer
    // (ButtonWords): what room is left, none needed.
    int hints = 0;
    for (GuestText& hint : g_main.hints) {
        hint.Make(56);
        hints += hint.at ? 1 : 0;
    }
    if (hints < controls::kButtonTexts)
        std::fprintf(stderr, "[pc settings] room for %d of %d longer button words\n", hints, controls::kButtonTexts);
    // Every text found room, or the list would be handed a null string.
    bool room = g_main.entries && g_main.helps && g_main.blank.at && g_main.question.at && g_main.quit.at &&
                g_main.quit_help.at && g_main.pc_settings.at && g_main.pc_settings_help.at;
    for (u32 i = 0; i < kEntries; ++i) room = room && g_main.row_names[i].at && g_main.row_helps[i].at;
    if (!room) std::fprintf(stderr, "[pc settings] the donors' room ran out: PC SETTINGS left out\n");
    g_main.made = room;
}

std::string OptionText(u32 i) {
    const Option& o = kOptions[i];
    return std::string(o.name) + ": " + o.values[o.now()];
}

const BindingsPage& Bindings() { return kBindingsPages[g_main.bindings]; }

const char* const kLayouts[] = {"CLASSIC", "MODERN", "CUSTOM"};

/* A seated device's name, as a row shows it. */
std::string SeatName(u32 id) {
    for (const ps2host::HostDevice& d : ps2host::HostDevices())
        if (d.id == id) {
            std::string name = d.name;
            for (char& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            return name;
        }
    return "GONE";
}

/* A row's words and its description. */
std::pair<std::string, std::string> ItemText(int item) {
    const Item& it = g_main.items[std::size_t(item)];
    switch (it.kind) {
        case Item::kPageLink:
            if (ps2host::OnPhone() && kPages[it.index].page == Page::kVideo)
                return {kPages[it.index].name, "FRAME RATE, RESOLUTION, SHAPE AND SMOOTHING"};   // (no window)
            return {kPages[it.index].name, kPages[it.index].help};
        case Item::kOption: return {OptionText(u32(it.index)), kOptions[it.index].help};
        case Item::kBindingsLink: return {kBindingsPages[it.index].name, kBindingsPages[it.index].help};
        case Item::kLayout: {
            const bool xbox = controls::PromptStyle() == controls::Style::kXbox;
            return {std::string("LAYOUT: ") + kLayouts[int(controls::RacingLayout())],
                    xbox ? "THE GAME'S OWN (CLASSIC), OR RT TO ACCELERATE AND LT TO BRAKE (MODERN)"
                         : "THE GAME'S OWN (CLASSIC), OR R2 TO ACCELERATE AND L2 TO BRAKE (MODERN)"};
        }
        case Item::kReset: return {"RESET TO DEFAULTS", "EVERY KEY OR BUTTON ON THIS PAGE AS IT FIRST WAS"};
        case Item::kDevMenu: return {"DEVELOPER MENU", "THE GAME'S HIDDEN MENU: UNLOCK ALL, LOCK ALL, THE AWARDS"};
        case Item::kMode: return {kModes[it.index].name, kModes[it.index].help};
        case Item::kPlayer: {
            const std::string player = "PLAYER " + std::to_string(it.index + 1) + ": ";
            if (it.index < int(g_main.seated.size())) {
                const std::string name = SeatName(g_main.seated[std::size_t(it.index)]);
                return {player + name.substr(0, kRowChars - player.size()),
                        "JOINED. CONTINUE TO PLAY, OR BACK TO START AGAIN"};
            }
            const std::string key = controls::BoundName(controls::Device::kKeyboard, controls::Context::kMenus, 3);
            return {player + "PRESS START", "PRESS START ON A CONTROLLER, OR " + key + " ON THE KEYBOARD, TO JOIN"};
        }
        case Item::kPlay:
            return {"CONTINUE", g_main.seated.empty() ? "FIRST, PRESS START TO JOIN"
                                                      : "TO MULTIPLAYER'S RACES WITH THESE PLAYERS"};
        case Item::kAction: break;
    }
    const BindingsPage& page = Bindings();
    const controls::Action& action = controls::ActionAt(page.device, page.context, it.index);
    const std::string name = controls::ActionName(page.device, page.context, it.index);
    const bool keyboard = page.device == controls::Device::kKeyboard;
    if (item == g_main.capturing) {
        const std::string wait = keyboard ? "PRESS A KEY" : controls::ControllerPresent() ? "PRESS A BUTTON" : "NO CONTROLLER";
        return {name + ": " + wait + " (" + std::to_string(std::max(1, g_main.seconds)) + ")",
                keyboard ? "THE KEY TO USE. ESCAPE OR A RIGHT CLICK LEAVES IT AS IT WAS"
                         : "THE BUTTON TO USE. WAITING, ESCAPE OR A RIGHT CLICK KEEPS THE OLD ONE"};
    }
    return {name + ": " + controls::BoundName(page.device, page.context, action.input), action.help};
}

/* Whether a row can be chosen (the list draws one that cannot greyed). */
bool ItemOpen(int item) {
    const Item& it = g_main.items[std::size_t(item)];
    if (it.kind == Item::kMode) return kModes[it.index].open();
    if (it.kind == Item::kPlay) return !g_main.seated.empty();
    return true;
}

/* The page's rows (g_main.items). */
void BuildItems() {
    std::vector<Item>& items = g_main.items;
    items.clear();
    switch (g_main.page) {
        case Page::kMultiplayer:
            for (int i = 0; i < kModeCount; ++i) items.push_back({Item::kMode, i});
            break;
        case Page::kPlayers:
            for (int i = 0; i < kPlayers; ++i) items.push_back({Item::kPlayer, i});
            items.push_back({Item::kPlay, 0});
            break;
        case Page::kPcSettings:
            for (u32 i = 0; i < kPageCount; ++i)
                if (Shown(kPages[i])) items.push_back({Item::kPageLink, int(i)});
            items.push_back({Item::kDevMenu, 0});
            break;
        case Page::kBindings: {
            const BindingsPage& page = Bindings();
            if (page.device == controls::Device::kController && page.context == controls::Context::kRacing)
                items.push_back({Item::kLayout, 0});
            for (int i = 0; i < controls::ActionCount(page.device, page.context); ++i) items.push_back({Item::kAction, i});
            items.push_back({Item::kReset, 0});
            break;
        }
        case Page::kControls:
            for (int i = 0; i < kBindingsPageCount; ++i)
                if (Shown(kBindingsPages[i])) items.push_back({Item::kBindingsLink, i});
            [[fallthrough]];
        default:
            for (u32 i = 0; i < kOptionCount; ++i)
                if (kOptions[i].page == g_main.page && Shown(kOptions[i])) items.push_back({Item::kOption, int(i)});
            break;
    }
}

/* The list (0x01E89544, its entries at +0x10) draws each row's words once,
   and again only when the row is highlighted or left: 0x0031FE40(list, row,
   enabled), which the game calls to enable or disable a row, draws it again
   (0x00320EB0) - every row, its words changed. */
constexpr u32 kList = 0x01E89544u;
void RefreshList(lifted::Context& c);

/* The rows shown from `scroll` written into the list's, and drawn again. */
void Render(lifted::Context& c) {
    const int n = int(g_main.items.size());
    for (u32 row = 0; row < kEntries; ++row) {
        const int item = g_main.scroll + int(row);
        if (item < n) {
            const auto [name, help] = ItemText(item);
            g_main.row_names[row].Set(name);
            g_main.row_helps[row].Set(help);
            generated::Write32(g_main.entries + 8 * row, g_main.row_names[row].at);
            generated::Write32(g_main.helps + 4 * row, g_main.row_helps[row].at);
            generated::Write32(g_main.entries + 8 * row + 4, ItemOpen(item) ? 0 : 1);
        } else {
            generated::Write32(g_main.entries + 8 * row, g_main.blank.at);
            generated::Write32(g_main.helps + 4 * row, g_main.blank.at);
            generated::Write32(g_main.entries + 8 * row + 4, 0);
        }
    }
    RefreshList(c);
    // The highlight's brackets are placed when the selection moves
    // (0x0031FF80, the list's +0xA4 against +0xA8): the row taken as
    // unselected, it is selected again - the brackets around its new words,
    // not the old ones' width. Another row highlighted (a page shown): the
    // list moves them itself, the old row drawn plain again - which taking
    // it as unselected would skip.
    if (generated::ReadU32(kList + 0xA4u) == generated::ReadU16(g_main.menu + 0x14))
        generated::Write32(kList + 0xA4u, ~0u);
}

/* 0x00271A00(holder, id): see GuestPool. */
constexpr u32 kQuestion = 0x7F02;           // our string id (past the table's 4,006): a YES/NO box's question

std::string Text16(u32 at, u32 most) {
    std::string text;
    for (u32 i = 0; at && i < most; ++i) {
        const u16 ch = generated::ReadU16(at + 2 * i);
        if (!ch) break;
        text += char(ch < 128 ? ch : '?');
    }
    return text;
}

/* The game's words naming a button (the race's hints, the title's PRESS
   START), looked up (v0 the string): as the controls have it. Words no
   longer than the game's are written over them, in the language file (the
   title's are asked for before the port's own words have room - the first
   main menu); longer ones are the port's (g_main.hints). The game's words
   are kept, by where they were, to go back to. */
void ButtonWords(lifted::Context& c) {
    static std::map<u32, std::string> originals;
    const u32 at = u32(c.r[2]);
    if (!at || (generated::ReadU16(at) != 'P' && generated::ReadU16(at) != 'W' && !originals.count(at))) return;
    const auto kept = originals.find(at);
    const std::string game = kept != originals.end() ? kept->second : Text16(at, 64);
    int slot = 0;
    const std::string words = controls::ButtonText(game, ControllerPrompts(), &slot);
    if (words.empty()) return;
    auto write = [&](const std::string& text) {
        if (Text16(at, 64) == text) return;
        u32 i = 0;
        for (; i < text.size(); ++i) generated::Write16(at + 2 * i, u8(text[i]));
        generated::Write16(at + 2 * i, 0);
    };
    if (words.size() <= game.size()) {
        if (words != game) originals.emplace(at, game);
        write(words);
        return;
    }
    if (kept != originals.end()) write(game);
    GuestText& t = g_main.hints[slot];
    if (!g_main.made || !t.at) return;
    t.Set(words);
    c.r[2] = t.at;
}

void GetString(lifted::Context& c) {
    const u32 id = u32(c.r[5]), caller = u32(c.r[31]);
    // The main menu's ONLINE as PC SETTINGS: asked from 0x003183D0 (it
    // alone; other screens keep their ONLINE), and its description.
    if ((id == kMainMenuOnline && caller == 0x003183D0u) || id == kOnlineHelp || id == kQuestion) {
        MakeTexts(c);
        const GuestText& t = id == kMainMenuOnline ? g_main.pc_settings
                           : id == kOnlineHelp     ? g_main.pc_settings_help
                                                   : g_main.question;
        if (t.at) {
            c.r[2] = t.at;
            return;
        }
    }
    F_00271A00(c);
    ButtonWords(c);
}

/* Calls a guest function from a host one, its arguments in a0-t1, and gives
   back v0; the caller's registers as they were. */
u32 GuestCall(lifted::Context& c, u32 target, std::initializer_list<u32> arguments) {
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    int r = 4;
    for (u32 a : arguments) c.r[r++] = s64(s32(a));
    lifted::Call(c, target, u32(saved[31]));
    const u32 result = u32(c.r[2]);
    std::memcpy(c.r, saved, sizeof saved);
    return result;
}

void RefreshList(lifted::Context& c) {
    // Each row enabled, or greyed: its entry's disabled byte.
    for (u32 row = 0; row < kEntries; ++row)
        GuestCall(c, 0x0031FE40u, {kList, row, generated::ReadU8(g_main.entries + 8 * row + 4) ? 0u : 1u});
}

}  // namespace
void ExtrasListMade(lifted::Context& c);     // extras.cpp
namespace {

/* 0x00321470(list, &index, entries): the main menu's list (0x00318340 sets
   it up from 0x00318460, the index and the count at menu+0x14, its five
   entries - a string pointer and a disabled byte each - at menu+0x18). It
   gets six: the game's five and QUIT GAME, in an array of the port's.
   DRIVER DETAILS > EXTRAS's goes without the NFS demo's row (extras.cpp). */
void MenuList(lifted::Context& c) {
    ExtrasListMade(c);
    if (u32(c.r[31]) == 0x00318464u) {
        MakeTexts(c);
        if (g_main.made) {
            const u32 from = u32(c.r[6]);
            g_main.menu = u32(c.r[5]) - 0x14;
            for (u32 i = 0; i < 8 * (kEntries - 1); i += 4)
                generated::Write32(g_main.entries + i, generated::ReadU32(from + i));
            generated::Write32(g_main.entries + 8 * kQuitEntry, g_main.quit.at);
            generated::Write32(g_main.entries + 8 * kQuitEntry + 4, 0);
            generated::Write16(u32(c.r[5]) + 2, kEntries);      // the count
            c.r[6] = g_main.entries;
            g_main.page = Page::kMainMenu;
            g_main.asking = Asking::kNone;
            if (controls::Capturing()) controls::CancelCapture();
            g_main.capturing = -1;
            ps2host::ResetPortDevices();        // back from MULTIPLAYER: every device player 1's again
            if (LanEnabled()) LanStopLobby();   // back from LAN: the lobby this game served, closed
        }
    }
    F_00321470(c);
}

/* 0x0031C350(ticker, &index, helps, 0): the main menu's descriptions, a
   string pointer each (0x00318340, from 0x003184A4): six, as the list. */
void MenuHelps(lifted::Context& c) {
    if (u32(c.r[31]) == 0x003184A8u && g_main.made) {
        const u32 from = u32(c.r[6]);
        for (u32 i = 0; i < kEntries - 1; ++i) generated::Write32(g_main.helps + 4 * i, generated::ReadU32(from + 4 * i));
        generated::Write32(g_main.helps + 4 * kQuitEntry, g_main.quit_help.at);
        c.r[6] = g_main.helps;
    }
    F_0031C350(c);
}

/* A page of PC SETTINGS in the list's rows (the main menu's kept to come
   back to), the item `highlighted` picked and in view; or the main menu
   again, its entry `highlighted` picked. */
void ShowPage(lifted::Context& c, Page page, int highlighted = 0) {
    const u32 menu = g_main.menu;
    const bool was_main = g_main.page == Page::kMainMenu, on = page != Page::kMainMenu;
    if (on) {
        if (was_main) {
            for (u32 i = 0; i < kEntries; ++i) {
                g_main.game_entries[i][0] = generated::ReadU32(g_main.entries + 8 * i);
                g_main.game_entries[i][1] = generated::ReadU32(g_main.entries + 8 * i + 4);
                g_main.game_helps[i] = generated::ReadU32(g_main.helps + 4 * i);
            }
        }
        g_main.page = page;
        BuildItems();
        const int n = int(g_main.items.size());
        highlighted = std::clamp(highlighted, 0, n - 1);
        g_main.scroll = std::clamp(highlighted - int(kEntries) + 1, 0, std::max(0, n - int(kEntries)));
        // The list keeps its six rows (it is set up with them): any past the
        // page's are blank, and the count Up and Down keep to is its.
        generated::Write16(menu + 0x16, u16(std::min(n, int(kEntries))));
        generated::Write16(menu + 0x14, u16(highlighted - g_main.scroll));
        Render(c);
    } else {
        for (u32 i = 0; i < kEntries; ++i) {
            generated::Write32(g_main.entries + 8 * i, g_main.game_entries[i][0]);
            generated::Write32(g_main.entries + 8 * i + 4, g_main.game_entries[i][1]);
            generated::Write32(g_main.helps + 4 * i, g_main.game_helps[i]);
        }
        generated::Write16(menu + 0x16, kEntries);
        generated::Write16(menu + 0x14, u16(highlighted));
        g_main.page = page;
        RefreshList(c);
        if (generated::ReadU32(kList + 0xA4u) == generated::ReadU16(menu + 0x14)) generated::Write32(kList + 0xA4u, ~0u);
    }
    if (on == !was_main) return;            // from one page to another: the legend stays
    // The button legend along the bottom (0x0031ECD0 on 0x01E89800, its
    // buttons as flags): the main menu's 0x104 (Change, Select); a
    // screen within a menu adds 0x1, (Triangle) Back. A button's sprite
    // and text (the legend's +0x1C and +0x48, a word per slot) stay once
    // made, so Back's (slot 0) go back to the frontend's pool (0x001D7560
    // on 0x01E903E0, by the legend's layer) as the main menu returns.
    constexpr u32 kLegend = 0x01E89800u;
    if (!on) {
        const u32 layer = u32(s32(generated::ReadS16(kLegend + 0x8u)));
        for (const u32 at : {kLegend + 0x1Cu, kLegend + 0x48u}) {
            if (const u32 object = generated::ReadU32(at)) {
                GuestCall(c, 0x001D7560u, {0x01E903E0u, layer, object});
                generated::Write32(at, 0);
            }
        }
    }
    GuestCall(c, 0x0031ECD0u, {kLegend, on ? 0x105u : 0x104u, 3u});
}

/* The item highlighted: the list's row, past the rows scrolled by. */
int Highlighted() { return g_main.scroll + int(generated::ReadU16(g_main.menu + 0x14)); }

/* 0x00319360 and 0x00319330 (selector): Up and Down on a menu's selector
   (the index, then the count), every screen's - refusing to go past either
   end, 0 then (1 moved). On a page longer than the list's six rows, Up on
   the top row or Down on the bottom one scrolls instead: the rows move by
   one under a highlight that stays. */
template <void (*Move)(lifted::Context&), int kStep>
void SelectorMove(lifted::Context& c) {
    const u32 selector = u32(c.r[4]);
    // PS2HOST_MOUSE_PROBE=1: every move of a frontend selector and the code
    // making it, to find a menu the mouse does not know yet.
    static const bool probe = std::getenv("PS2HOST_MOUSE_PROBE") != nullptr;
    if (probe)
        std::fprintf(stderr, "[probe] selector 0x%08X (%u of %u) moved from 0x%08X\n", unsigned(selector),
                     unsigned(generated::ReadU16(selector)), unsigned(generated::ReadU16(selector + 2)),
                     unsigned(c.r[31]));
    if (g_main.made && g_main.page != Page::kMainMenu && selector == g_main.menu + 0x14 && g_main.capturing < 0) {
        const int index = generated::ReadU16(selector), n = int(g_main.items.size());
        const bool down = kStep > 0 && index == int(kEntries) - 1 && g_main.scroll + int(kEntries) < n;
        const bool up = kStep < 0 && index == 0 && g_main.scroll > 0;
        if (down || up) {
            g_main.scroll += kStep;
            Render(c);
            c.r[2] = 1;
            return;
        }
    }
    Move(c);
}

/* The frontend's message box (0x0032D080 shows one: the box, its type -
   7 is YES/NO - and the message's string id). Its +0x0A is set while it
   shows, +0x0C the button highlighted (0 the first); 0x0032B2C0(box, pad,
   1) takes a frame's input and returns the button pressed (its id from the
   type's table at 0x004A4A10: YES 4, NO 5) or 0xF; its method +0x0C
   closes it. */
constexpr u32 kMessageBox = 0x01E898E8;
constexpr u32 kYes = 4, kNo = 5;

/* Asks a YES/NO question, NO highlighted (MainMenu takes the answer). */
void Ask(lifted::Context& c, Asking asking, const char* question) {
    g_main.question.Set(question);
    GuestCall(c, 0x0032D080u, {kMessageBox, 7, kQuestion, 0xF, 0});
    generated::Write16(kMessageBox + 0x0C, 1);
    g_main.asking = asking;
}

bool TrianglePressed() {
    static bool before = false;
    const bool now = (ps2host::PadButtonsHeld() & (1u << 12)) != 0;
    const bool pressed = now && !before;
    before = now;
    return pressed;
}

/* Back a page: a page of bindings to CONTROLS, a page of settings to PC
   SETTINGS, it to the main menu - the row it came from highlighted. */
void GoBack(lifted::Context& c) {
    switch (g_main.page) {
        case Page::kBindings: ShowPage(c, Page::kControls, BindingsRow(g_main.bindings)); return;
        case Page::kPcSettings: ShowPage(c, Page::kMainMenu, kPcSettingsEntry); return;
        case Page::kMultiplayer: ShowPage(c, Page::kMainMenu, kMultiplayerEntry); return;
        case Page::kPlayers: ShowPage(c, Page::kMultiplayer); return;
        default: {
            int from = 0;
            for (u32 i = 0; i < kPageCount; ++i)
                if (kPages[i].page == g_main.page) from = PageRow(i);
            ShowPage(c, Page::kPcSettings, from);
        }
    }
}

/* PLAYERS shown, no one seated yet; what each device holds now taken as
   held before (a Start held on the way in joins no one). */
void ShowPlayers(lifted::Context& c) {
    g_main.seated.clear();
    g_main.start_before.clear();
    g_main.cross_before = true;
    ShowPage(c, Page::kPlayers);
}

/* Whether a device holds Start, as the player's menu controls have it. */
bool HoldsStart(u32 id) {
    if (id == ps2host::kKeyboardDevice) {
        const int key = controls::MenuBinding(controls::Device::kKeyboard, 3);
        return key > 0 && (ps2host::WindowFocused() || ps2host::KeysScripted()) && ps2host::HostKeyDown(key);
    }
    ps2host::HostPadState pad;
    if (!ps2host::HostDevicePad(id, &pad)) return false;
    const int button = id >= ps2host::kScriptedDevice ? 3 : controls::MenuBinding(controls::Device::kController, 3);
    return button >= 0 && (pad.buttons & (1u << button)) != 0;
}

/* PLAYERS, each frame: a device pressing Start takes the next seat. */
void SeatPlayers(lifted::Context& c) {
    bool changed = false;
    for (const ps2host::HostDevice& d : ps2host::HostDevices()) {
        const bool held = HoldsStart(d.id);
        const auto before = g_main.start_before.find(d.id);
        const bool pressed = held && before != g_main.start_before.end() && !before->second;
        g_main.start_before[d.id] = held;
        if (!pressed || int(g_main.seated.size()) >= kPlayers) continue;
        if (std::find(g_main.seated.begin(), g_main.seated.end(), d.id) != g_main.seated.end()) continue;
        g_main.seated.push_back(d.id);
        std::fprintf(stderr, "[players] player %d: %s\n", int(g_main.seated.size()), d.name.c_str());
        changed = true;
    }
    // A seat whose controller is unplugged is free again.
    const std::vector<ps2host::HostDevice> devices = ps2host::HostDevices();
    for (auto seat = g_main.seated.begin(); seat != g_main.seated.end();) {
        const bool there = std::any_of(devices.begin(), devices.end(),
                                       [&](const ps2host::HostDevice& d) { return d.id == *seat; });
        if (there) {
            ++seat;
        } else {
            seat = g_main.seated.erase(seat);
            changed = true;
        }
    }
    if (changed) Render(c);
}

/* CONTINUE: each seated player's device on their controller port, and on to
   the game's own MULTIPLAYER - as the main menu's Cross on it goes (its
   entry at menu+4, then the frontend's fade, 0x0030D560). */
void PlaySplitscreen(lifted::Context& c) {
    if (g_main.seated.empty()) return;
    for (int player = 0; player < kPlayers; ++player)
        ps2host::SetPortDevices(u32(player), player < int(g_main.seated.size())
                                                 ? std::vector<u32>{g_main.seated[std::size_t(player)]}
                                                 : std::vector<u32>{});
    ShowPage(c, Page::kMainMenu, kMultiplayerEntry);
    generated::Write32(g_main.menu + 0x4, kMultiplayerEntry);
    GuestCall(c, 0x0030D560u, {0x01E90240u, 0, 0});
}

/* LAN: the game's own ONLINE, as the main menu's row 3 opens it - its exit
   (0x0030E340) copies the profile's name, sets the main mode (3) and brings
   the online flow up - on the PC's network (lan.cpp, lan_lobby.cpp, the
   engine's iop_inet.cpp). Its first-time message (0x01E3D595 clear: string
   0xF9D, European players on another television standard) is no LAN's. */
void PlayLan(lifted::Context& c) {
    LanPresetLogin();
    ShowPage(c, Page::kMainMenu, kMultiplayerEntry);
    generated::Write8(0x01E3D595u, 1);
    generated::Write32(g_main.menu + 0x4, kPcSettingsEntry);   // ONLINE's row (PC SETTINGS in our list)
    GuestCall(c, 0x0030D560u, {0x01E90240u, 0, 0});
    std::fprintf(stderr, "[lan] MULTIPLAYER: LAN -> the online flow\n");
}

/* Cross on a row of a page. */
void Choose(lifted::Context& c, int item) {
    if (item < 0 || item >= int(g_main.items.size())) return;
    const Item it = g_main.items[std::size_t(item)];
    if (!ItemOpen(item)) return;
    switch (it.kind) {
        case Item::kPageLink: ShowPage(c, kPages[it.index].page); return;
        case Item::kBindingsLink:
            g_main.bindings = it.index;
            ShowPage(c, Page::kBindings);
            return;
        case Item::kOption: {
            const Option& o = kOptions[it.index];
            o.set((o.now() + 1) % o.count);
            Render(c);
            return;
        }
        case Item::kLayout:
            // CLASSIC and MODERN in turn; the player's own asked about first.
            if (controls::RacingLayout() == controls::Layout::kCustom) {
                Ask(c, Asking::kLayout, "Replace your own layout?");
                return;
            }
            controls::SetRacingLayout(controls::RacingLayout() == controls::Layout::kClassic ? controls::Layout::kModern
                                                                                              : controls::Layout::kClassic);
            Render(c);
            return;
        case Item::kReset: Ask(c, Asking::kReset, "Reset these controls?"); return;
        case Item::kDevMenu: OpenDevMenu(c); return;
        case Item::kMode:
            if (it.index == kSplitscreenMode) ShowPlayers(c);
            else if (it.index == kLanMode) PlayLan(c);
            return;
        case Item::kPlayer:
        case Item::kPlay: PlaySplitscreen(c); return;
        case Item::kAction: {
            const BindingsPage& page = Bindings();
            controls::BeginCapture(page.device, page.context, controls::ActionAt(page.device, page.context, it.index).input);
            g_main.capturing = item;
            controls::Capture(&g_main.seconds);
            Render(c);
            return;
        }
    }
}

/* 0x003181E0(menu), the main menu's input each frame: Cross (0x0022CFE0) on
   an entry opens it (0x0030D560), Up and Down move. Cross on PC SETTINGS
   lists its pages, on a page its rows; on a setting, it switches it (and
   saves it, ps2host/settings.h); on a binding, the next key or button
   pressed is its (controls.h) - the row counting down meanwhile. Triangle
   goes back a page. Cross on QUIT GAME asks first. */
void MainMenuInput(lifted::Context& c) {
    const u32 menu = u32(c.r[4]);
    const bool ours = g_main.made && menu == g_main.menu;
    const bool back = TrianglePressed();
    // Back from the development menu: PC SETTINGS again, DEVELOPER MENU picked.
    if (ours && DevMenuReturned()) {
        ShowPage(c, Page::kPcSettings, PageRow(kPageCount));
        c.r[2] = 1;
        return;
    }
    const bool settings = g_main.page != Page::kMainMenu;
    if (ours && settings && g_main.capturing >= 0) {
        // A binding being made has the input: its row shows how it stands.
        int seconds = 0;
        const controls::CaptureState state = controls::Capture(&seconds);
        if (state == controls::CaptureState::kWaiting) {
            if (seconds != g_main.seconds) {
                g_main.seconds = seconds;
                Render(c);
            }
        } else {
            g_main.capturing = -1;
            Render(c);              // the row's new key, and any it was swapped with
        }
        c.r[2] = 1;
        return;
    }
    if (ours && settings && back) {
        GoBack(c);
        c.r[2] = 1;
        return;
    }
    // PLAYERS: Start seats a device; Cross (newly pressed - Start, which the
    // game takes as Cross too, does not count) continues once one is.
    if (ours && g_main.page == Page::kPlayers) {
        SeatPlayers(c);
        const bool cross = (ps2host::PadButtonsHeld() & (1u << 14)) != 0;
        const bool pressed = cross && !g_main.cross_before;
        g_main.cross_before = cross;
        if (pressed && !g_main.seated.empty()) PlaySplitscreen(c);
        // Up and Down still move the highlight.
        if (GuestCall(c, 0x0022CEE0u, {0x004F50C0u, 0})) GuestCall(c, 0x00319360u, {menu + 0x14});
        else if (GuestCall(c, 0x0022CE60u, {0x004F50C0u, 0})) GuestCall(c, 0x00319330u, {menu + 0x14});
        c.r[2] = 1;
        return;
    }
    if (ours && GuestCall(c, 0x0022CFE0u, {0x004F50C0u, 0})) {
        const u32 entry = generated::ReadU16(menu + 0x14);
        if (settings) {
            Choose(c, Highlighted());
            c.r[2] = 1;
            return;
        }
        if (entry == kPcSettingsEntry) {
            ShowPage(c, Page::kPcSettings);
            c.r[2] = 1;
            return;
        }
        if (entry == kMultiplayerEntry) {
            ShowPage(c, Page::kMultiplayer);
            c.r[2] = 1;
            return;
        }
        if (entry == kQuitEntry) {
            Ask(c, Asking::kQuit, "Quit Burnout 3?");
            c.r[2] = 1;
            return;
        }
    }
    F_003181E0(c);
}

/* 0x00318100(menu), the main menu each frame: by its +0x10, the menu's
   input (1), or a message box's (0 and 2: the online flow's) until a button
   closes it. While a box of the port's shows it has the input: YES does
   what it asked (QUIT GAME's ends the game, as closing the window does), NO
   or Triangle closes the box. */
void MainMenu(lifted::Context& c) {
    if (g_main.asking == Asking::kNone) {
        F_00318100(c);
        return;
    }
    const u32 pressed = GuestCall(c, 0x0032B2C0u, {kMessageBox, 0, 1});
    if (pressed == kYes && g_main.asking == Asking::kQuit) throw ps2host::GuestExit{0};
    if (pressed == kYes || pressed == kNo || TrianglePressed()) {
        GuestCall(c, generated::ReadU32(generated::ReadU32(kMessageBox) + 0x0C), {kMessageBox});
        if (pressed == kYes) {
            const BindingsPage& page = Bindings();
            if (g_main.asking == Asking::kReset) controls::ResetDefaults(page.device, page.context);
            else controls::SetRacingLayout(controls::Layout::kClassic);
            Render(c);
        }
        g_main.asking = Asking::kNone;
    }
}

/* PC SETTINGS' pages but VIDEO (the panel's own first rows) in the engine's
   panel too (F9, ps2host::AddPortOptions): the same rows, reached in a race. */
void AddPanelPages() {
    std::vector<ps2host::PortOption> options;
    for (const Option& o : kOptions) {
        if (o.page == Page::kVideo || !Shown(o)) continue;
        ps2host::PortOption p;
        for (const PageInfo& info : kPages)
            if (info.page == o.page) p.page = info.name;
        p.name = o.name;
        p.help = o.help;
        p.values.assign(o.values, o.values + o.count);
        p.now = o.now;
        p.set = o.set;
        options.push_back(p);
    }
    ps2host::AddPortOptions(options);
}

}  // namespace

void RegisterGameIopServers();     // iop_servers.cpp
void RegisterWidescreen();         // widescreen.cpp
void RegisterMouse();              // mouse.cpp
void RegisterControls();           // controls.cpp
void RegisterSixtyFps();           // sixty_fps.cpp
void RegisterSingleEvent();        // single_event.cpp
void RegisterExtraSeries();        // extra_series.cpp
void RegisterHighFps();            // high_fps.cpp
void RegisterDevMenu();            // dev_menu.cpp
void RegisterMods();               // mods.cpp
void RegisterGraphics();           // graphics.cpp
void RegisterLan();                // lan.cpp
void RegisterCustomSongs();        // custom_songs.cpp

void RegisterGameOverrides() {
    // VU1's fast mode: every scripted run draws every picture as the exact
    // VU does (regress.py --vu-fast), for ~15% less of VIF1's worker - the
    // busiest thread on a slow machine.
    ps2host::SetVuFastDefault(true);
    ps2host::SetCaptureHook(CaptureHang);
    // Frame pacing off (no PC SETTINGS row): the least delay; frame_pacing=on
    // in the settings file still turns it on.
    ps2host::SetFramePacingDefault(false);
    // A frame's picture at its blank's end (SetPresentAtBlankEnd): the game
    // waits for the blank, then copies its finished frame (0x46) to the
    // displayed buffer (0) - in the blank, where the console shows it in the
    // very next field. Taken at the blank's start, every picture went out a
    // frame late.
    ps2host::SetPresentAtBlankEnd(true);
    // ANTIALIASING has no SSAA 4X any more: a settings file's is 2X.
    if (ps2host::Setting("antialiasing", "off") == "ssaa4") ps2host::SetSetting("antialiasing", "ssaa2");
    // The GPU pipelines the game draws with, made in the background from the
    // start: none made in the middle of a player's first race (up to 0.7 s
    // each on an integrated GPU's cold driver).
    ps2host::SetGpuPipelineHints(kGpuPipelineHints, sizeof kGpuPipelineHints / sizeof kGpuPipelineHints[0]);
    AddPanelPages();
    RegisterGameIopServers();
    RegisterWidescreen();
    RegisterMouse();
    RegisterControls();
    RegisterSixtyFps();
    RegisterSingleEvent();
    RegisterExtraSeries();
    RegisterHighFps();
    RegisterDevMenu();
    RegisterMods();
    RegisterGraphics();
    RegisterCustomSongs();
    RegisterLan();
    using namespace ps2host::mc2;
    ps2host::Override(0x00244218, CardOr<GetInfo, F_00244218>, "libmc2 GetInfo (host card)");
    ps2host::Override(0x002445D0, CardOr<Format, F_002445D0>, "libmc2 Format (host card)");
    ps2host::Override(0x00246588, CardOr<ReadFile, F_00246588>, "libmc2 ReadFile (host card)");
    ps2host::Override(0x00246788, CardOr<WriteFile, F_00246788>, "libmc2 WriteFile (host card)");
    ps2host::Override(0x00245FC0, CardOr<Create, F_00245FC0>, "libmc2 Create/Mkdir (host card)");
    ps2host::Override(0x002470D8, CardOr<GetDir, F_002470D8>, "libmc2 GetDir (host card)");
    ps2host::Override(0x00245398, CardOr<Lookup, F_00245398>, "libmc2 lookup (host card)");
    // RenderWare's wait for its DMA packet buffers to flip (0x001F2408, the
    // loop at 0x001F24E0: two globals the DMA-end interrupt updates, and a
    // callback each turn): a third of the race's time under --virtual-time
    // went to spinning there (--sample); in real time, 60% of the EE's in the
    // menus, a core spun while it waited for its next picture's blank.
    ps2host::AddIdleLoop(0x001F24E0u);
    ps2host::Override(0x001F2408u, WaitsForInterrupt<F_001F2408>, "RenderWare's packet wait: asleep in real time");
    ps2host::Override(0x002AF340u, RwaAnswer, "RenderWare Audio's answers: a dropped one said, the last kept");
    ps2host::Override(0x002AEFC0u, RwaCommand, "RenderWare Audio's commands: the last kept");
    ps2host::Override(0x003FB0F0u, MusicStart, "the EA Trax player's step: a load's wait for its start given up after 3 s");
    ps2host::Override(0x00228040u, PacketWaitTurn, "the packet wait's turn: asleep until an event in real time");
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
    ps2host::Override(0x001313C0u, SkipMovie, "a movie screen's update: Start skips the movie");
    ps2host::Override(0x0019A950u, FastLoading<F_0019A950, 0x0019A950u>, "a race's loading: fast-forwarded while it waits");
    ps2host::Override(0x001322B0u, FastLoading<F_001322B0, 0x001322B0u>, "the game's loading: fast-forwarded while it waits");
    ps2host::Override(0x00271A00u, GetString, "the string lookup: PC SETTINGS' words");
    ps2host::Override(0x003181E0u, MainMenuInput, "the main menu's input: PC SETTINGS, QUIT GAME");
    ps2host::Override(0x00318100u, MainMenu, "the main menu: QUIT GAME's message box");
    ps2host::Override(0x00321470u, MenuList, "a list: the main menu's with QUIT GAME");
    ps2host::Override(0x0031C350u, MenuHelps, "a ticker: the main menu's descriptions with QUIT GAME's");
    ps2host::Override(0x00319360u, SelectorMove<F_00319360, -1>, "a selector's Up: PC SETTINGS' pages scroll");
    ps2host::Override(0x00319330u, SelectorMove<F_00319330, 1>, "a selector's Down: PC SETTINGS' pages scroll");
}

/* The music's watchdog (each vertical blank, in every mode - sixty_fps.cpp:
   not only in a race's frames: a load after a race waited on it): RenderWare
   Audio's stream threads wait at 0x00014848 (WaitSema on the stream's
   semaphore, back at 0x00014850) in a loop that checks the stream's flags
   again when woken - a wake-up too many is harmless there. One lost (#16:
   the music stopped mid race and a later load waited for its start) left
   them waiting for good; one waiting more than 2 s is woken, once a second
   looked at. Only where that wait is (the jal to WaitSema's stub), should
   the module ever load elsewhere. */
void MusicWatchdog() {
    static u64 last = 0;
    const u64 blank = ps2host::FrameNumber();
    if (blank - last < 60) return;
    last = blank;
    const u8* at = ps2host::GuestMemory().Translate(ps2host::kIopRamBase + 0x00014848u, 4);
    if (!at || at[0] != 0xF4 || at[1] != 0x62 || at[2] != 0x00 || at[3] != 0x0C) return;     // jal 0x00018BD0
    if (const int woken = ps2host::iop::WakeLongWaiters(0x00014850u, 2'000'000)) {
        // The first time, the memories as the stall is (stall_ee.bin,
        // stall_iop.bin beside the log): the streams' objects (+0x44 the
        // buffer waiting, +0x58 the flags, +0x4C the semaphore) as they were.
        static bool saved = false;
        if (!saved) {
            saved = true;
            SaveMemories("stall_ee.bin", "stall_iop.bin");
        }
        static int times = 0;
        if (++times <= 20)
            std::fprintf(stderr, "[music] %d of RenderWare Audio's stream threads woken: waiting 2 s on the stream's "
                         "semaphore (a lost wake-up)\n", woken);
    }
}
