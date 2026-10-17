/*
 * Burnout 3's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently (the GS, the
 * IOP's services) and the way enhancements hook in. Register them here with
 * ps2host::Override(address, function, name); see engine/docs/PORTING.md.
 */

#include <algorithm>
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
#include "ps2host/mc2.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// prompts.cpp: whether the controller's prompts show (the controller used last).
bool ControllerPrompts();

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
// The frame's post effects: glow and speed blur (see PostEffects).
void F_002FA9A0(lifted::Context& c);
void F_001322B0(lifted::Context& c);
// The frontend: its string lookup, the main menu's input, a message box.
void F_00271A00(lifted::Context& c);
void F_00318100(lifted::Context& c);
void F_003181E0(lifted::Context& c);
void F_00445530(lifted::Context& c);
void F_003C9BF0(lifted::Context& c);
void F_00321470(lifted::Context& c);
void F_0031C350(lifted::Context& c);
// A menu's selector: Up and Down (see SelectorMove).
void F_00319360(lifted::Context& c);
void F_00319330(lifted::Context& c);

bool LanEnabled();                 // iop_servers.cpp: the LAN work, on unless B3_LAN=0
void LanPresetLogin();             // lan.cpp: SELECT EA ACCOUNT logs in by itself
void LanStopLobby();               // lan_lobby.cpp: the lobby this game served, closed

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

/* The main loop's (0x00132600) loading states, each a function it calls
   once a frame until it returns non-zero (FINDINGS: "Loading a race"):
   0x0019A950 in state 4, a race's world, a phase a frame; 0x001322B0 in
   states 7 and 9, the game's own - the long load at boot, and after a race.
   Most of their time goes to frames going by and the IOP's streams, neither
   CPU busy. While one returns 0 the vertical blanks come faster
   (SetFastForward): the loading screen, a still picture and a spinner, goes
   by sooner. PS2HOST_LOAD_SPEED says how much faster (1: as the console). */
/* The frame's post effects (0x002FA9A0, from the race's and the frontend's
   3D drawing): the frame copied to a half-size buffer, its bright parts
   (glow) and a radial speed blur gathered there over frames, the buffer laid
   over the whole frame (0x002F9F50). At 320x224 that layer softens the
   picture at any internal resolution. BLUR OFF leaves the frame as drawn. */
void PostEffects(lifted::Context& c) {
    if (ps2host::Setting("blur", "original") == "off") return;
    F_002FA9A0(c);
}

/* A function that may wait in an idle loop: in real time the wait sleeps
   (ps2host::InterruptWait). */
template <void (*Wait)(lifted::Context&)>
void WaitsForInterrupt(lifted::Context& c) {
    ps2host::InterruptWait waiting;
    Wait(c);
}

template <void (*Load)(lifted::Context&)>
void FastLoading(lifted::Context& c) {
    static const char* forced = std::getenv("PS2HOST_LOAD_SPEED");
    const unsigned speed = forced ? unsigned(std::max(1, std::atoi(forced))) : 8u;
    Load(c);
    ps2host::SetFastForward(u32(c.r[2]) ? 1 : speed);
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
constexpr u32 kPcSettingsEntry = 3, kQuitEntry = 5;

/* PC SETTINGS' pages: first VIDEO, GRAPHICS and CONTROLS, Cross on one
   showing its rows - settings, each a few values, Cross going to the next
   (ON/OFF, or the internal resolution's three); CONTROLS' first four rows
   open the bindings' pages. Triangle goes back a page, as on the game's own
   screens (the legend says so): no BACK row. The list shows six rows (it is
   set up with them; a seventh shifts the main menu up): a page with more
   scrolls, Up past the top row or Down past the bottom one bringing the
   next into view. */
enum class Page { kMainMenu, kPcSettings, kVideo, kGraphics, kControls, kBindings };
struct PageInfo {
    Page page;
    const char* name;
    const char* help;
};
const PageInfo kPages[] = {
    {Page::kVideo, "VIDEO", "THE WINDOW, RESOLUTION, PICTURE SHAPE AND SMOOTHING"},
    {Page::kGraphics, "GRAPHICS", "THE GAME'S OWN EFFECTS"},
    {Page::kControls, "CONTROLS", "YOUR KEYS AND BUTTONS, AND THE BUTTONS SHOWN"},
};
constexpr u32 kPageCount = sizeof kPages / sizeof kPages[0];

struct Option {
    Page page;
    const char* name;
    const char* help;
    int (*now)();                           // the value's index
    void (*set)(int);
    const char* values[4];
    int count;
};
int Fullscreen() { return ps2host::WindowFullscreen() ? 1 : 0; }
void SetFullscreen(int on) { ps2host::SetWindowFullscreen(on != 0); }
int Vsync() { return ps2host::WindowVsync() ? 1 : 0; }
void SetVsync(int on) { ps2host::SetWindowVsync(on != 0); }
int FramePacing() { return ps2host::SettingOn("frame_pacing", true) ? 1 : 0; }
void SetFramePacing(int on) { ps2host::SetSettingOn("frame_pacing", on != 0); }
/* A setting's value among `values` (its index; the first if it is none of them). */
template <std::size_t N>
int Among(const char* key, const char* const (&values)[N]) {
    const std::string now = ps2host::Setting(key, values[0]);
    for (std::size_t i = 0; i < N; ++i)
        if (now == values[i]) return int(i);
    return 0;
}
const char* const kResolutions[] = {"original", "2x", "display"};
int Resolution() { return Among("resolution", kResolutions); }
void SetResolution(int which) { ps2host::SetSetting("resolution", kResolutions[which]); }
int Blur() { return ps2host::Setting("blur", "original") == "off" ? 1 : 0; }
void SetBlur(int off) { ps2host::SetSetting("blur", off ? "off" : "original"); }
const char* const kAspects[] = {"4:3", "16:9", "auto"};
int Aspect() { return Among("aspect", kAspects); }
void SetAspect(int which) { ps2host::SetWindowAspect(kAspects[which]); }
const char* const kAntiAliasings[] = {"off", "fxaa", "ssaa2", "ssaa4"};
int AntiAliasing() { return Among("antialiasing", kAntiAliasings); }
void SetAntiAliasing(int which) { ps2host::SetWindowAntiAliasing(kAntiAliasings[which]); }
const char* const kPromptStyles[] = {"auto", "playstation", "xbox"};
int ButtonPrompts() { return Among("button_prompts", kPromptStyles); }
void SetButtonPrompts(int which) { ps2host::SetSetting("button_prompts", kPromptStyles[which]); }
int TriggerEffects() { return ps2host::SettingOn("trigger_effects", true) ? 1 : 0; }
void SetTriggerEffects(int on) { ps2host::SetSettingOn("trigger_effects", on != 0); }
const Option kOptions[] = {
    {Page::kVideo, "FULLSCREEN", "THE WHOLE SCREEN OR A WINDOW (ALT+ENTER)", Fullscreen, SetFullscreen, {"OFF", "ON"}, 2},
    {Page::kVideo, "VERTICAL SYNC", "NO TEARING, OR THE LEAST DELAY", Vsync, SetVsync, {"OFF", "ON"}, 2},
    {Page::kVideo, "FRAME PACING", "PICTURES EVENLY SPACED, A LITTLE MORE DELAY", FramePacing, SetFramePacing,
     {"OFF", "ON"}, 2},
    {Page::kVideo, "RESOLUTION", "DRAWN AS THE PS2 DID, TWICE AS SHARP, OR AS SHARP AS THE SCREEN", Resolution,
     SetResolution, {"ORIGINAL", "2X", "DISPLAY"}, 3},
    {Page::kVideo, "ASPECT RATIO", "THE PICTURE'S SHAPE: A TELEVISION'S, WIDESCREEN, OR THE SCREEN'S OWN", Aspect,
     SetAspect, {"ORIGINAL (4:3)", "WIDESCREEN (16:9)", "AUTO"}, 3},
    {Page::kVideo, "ANTIALIASING", "EDGES AS DRAWN, SMOOTHED (FXAA), OR SUPERSAMPLED (SSAA)",
     AntiAliasing, SetAntiAliasing, {"OFF", "FXAA", "SSAA 2X", "SSAA 4X"}, 4},
    {Page::kGraphics, "BLUR", "THE SPEED BLUR AND GLOW, OR A CLEAR PICTURE", Blur, SetBlur, {"ORIGINAL", "OFF"}, 2},
    {Page::kControls, "BUTTON PROMPTS", "THE BUTTONS SHOWN: YOUR CONTROLLER'S, PLAYSTATION OR XBOX", ButtonPrompts,
     SetButtonPrompts, {"AUTO", "PLAYSTATION", "XBOX"}, 3},
    {Page::kControls, "TRIGGER EFFECTS", "DUALSENSE: THE TRIGGERS PUSH BACK WHEN THEY DRIVE", TriggerEffects,
     SetTriggerEffects, {"OFF", "ON"}, 2},
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

/* A row of a page, and what choosing it does. */
struct Item {
    enum Kind { kPageLink, kOption, kBindingsLink, kAction, kLayout, kReset } kind;
    int index;                  // kPages', kOptions', kBindingsPages' or the page's actions' index
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
    GuestText pc_settings, pc_settings_help, quit, quit_help, question, blank, lan;
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
    g_main.pc_settings.Set("PC SETTINGS");
    g_main.pc_settings_help.Make(32);
    g_main.pc_settings_help.Set("SET UP THE PC VERSION");
    g_main.quit.Make(16);
    g_main.quit.Set("QUIT GAME");
    g_main.quit_help.Make(32);
    g_main.quit_help.Set("CLOSE BURNOUT 3");
    g_main.question.Make(32);
    g_main.blank.Make(2);
    g_main.blank.Set(" ");               // (an empty string the list does not draw as empty)
    g_main.lan.Make(4);
    g_main.lan.Set("LAN");
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

constexpr u32 kLanString = 0x7F03;          // ours (past the table's 4,006): MULTIPLAYER's LAN

const BindingsPage& Bindings() { return kBindingsPages[g_main.bindings]; }

const char* const kLayouts[] = {"CLASSIC", "MODERN", "CUSTOM"};

/* A row's words and its description. */
std::pair<std::string, std::string> ItemText(int item) {
    const Item& it = g_main.items[std::size_t(item)];
    switch (it.kind) {
        case Item::kPageLink: return {kPages[it.index].name, kPages[it.index].help};
        case Item::kOption: return {OptionText(u32(it.index)), kOptions[it.index].help};
        case Item::kBindingsLink: return {kBindingsPages[it.index].name, kBindingsPages[it.index].help};
        case Item::kLayout: {
            const bool xbox = controls::PromptStyle() == controls::Style::kXbox;
            return {std::string("LAYOUT: ") + kLayouts[int(controls::RacingLayout())],
                    xbox ? "THE GAME'S OWN (CLASSIC), OR RT TO ACCELERATE AND LT TO BRAKE (MODERN)"
                         : "THE GAME'S OWN (CLASSIC), OR R2 TO ACCELERATE AND L2 TO BRAKE (MODERN)"};
        }
        case Item::kReset: return {"RESET TO DEFAULTS", "EVERY KEY OR BUTTON ON THIS PAGE AS IT FIRST WAS"};
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

/* The page's rows (g_main.items). */
void BuildItems() {
    std::vector<Item>& items = g_main.items;
    items.clear();
    switch (g_main.page) {
        case Page::kPcSettings:
            for (u32 i = 0; i < kPageCount; ++i) items.push_back({Item::kPageLink, int(i)});
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
            for (int i = 0; i < kBindingsPageCount; ++i) items.push_back({Item::kBindingsLink, i});
            [[fallthrough]];
        default:
            for (u32 i = 0; i < kOptionCount; ++i)
                if (kOptions[i].page == g_main.page) items.push_back({Item::kOption, int(i)});
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
        } else {
            generated::Write32(g_main.entries + 8 * row, g_main.blank.at);
            generated::Write32(g_main.helps + 4 * row, g_main.blank.at);
        }
        generated::Write32(g_main.entries + 8 * row + 4, 0);
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
    // B3_STRINGS=1 (the LAN work): each (id, caller) asked, once.
    static const bool tell = std::getenv("B3_STRINGS") != nullptr;
    if (tell) {
        static std::set<u64> seen;
        if (seen.insert((u64(id) << 32) | caller).second)
            std::fprintf(stderr, "[strings] 0x%X from %08X\n", id, caller - 8);
    }
    // The main menu's ONLINE as PC SETTINGS: asked from 0x003183D0 (it
    // alone; other screens keep their ONLINE), and its description.
    if ((id == kMainMenuOnline && caller == 0x003183D0u) || id == kOnlineHelp || id == kQuestion ||
        id == kLanString) {
        MakeTexts(c);
        const GuestText& t = id == kMainMenuOnline ? g_main.pc_settings
                           : id == kOnlineHelp     ? g_main.pc_settings_help
                           : id == kLanString      ? g_main.lan
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
    for (u32 row = 0; row < kEntries; ++row) GuestCall(c, 0x0031FE40u, {kList, row, 1});
}

/* 0x00321470(list, &index, entries): the main menu's list (0x00318340 sets
   it up from 0x00318460, the index and the count at menu+0x14, its five
   entries - a string pointer and a disabled byte each - at menu+0x18). It
   gets six: the game's five and QUIT GAME, in an array of the port's. */
void MenuList(lifted::Context& c) {
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
   back to), the item `highlighted` picked and in view; or the main menu again. */
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
        generated::Write16(menu + 0x14, kPcSettingsEntry);
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
        case Page::kBindings: ShowPage(c, Page::kControls, g_main.bindings); return;
        case Page::kPcSettings: ShowPage(c, Page::kMainMenu); return;
        default: {
            int from = 0;
            for (u32 i = 0; i < kPageCount; ++i)
                if (kPages[i].page == g_main.page) from = int(i);
            ShowPage(c, Page::kPcSettings, from);
        }
    }
}

/* Cross on a row of a page. */
void Choose(lifted::Context& c, int item) {
    if (item < 0 || item >= int(g_main.items.size())) return;
    const Item it = g_main.items[std::size_t(item)];
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

// ------------------------------------------------------------ MULTIPLAYER: LAN
//
// The LAN work (on unless B3_LAN=0): the main menu keeps PC SETTINGS; the game's
// online flow opens from a sixth row of MULTIPLAYER, LAN. That screen's
// list (0x01E97D08, made by 0x004456B0, room for ten rows) is set up by
// 0x003C9A60, which then draws it (0x00445530): its count at +4, the row taken at +8, the rows' string ids
// from +0x10, two "greyed" flags each at +0x38 and +0x42. The screen's
// handler (0x003C9BF0) takes a row on message 5, event 0, with its +0x10 at 1.

constexpr u32 kModeList = 0x01E97D08u;
constexpr u32 kModeLanRow = 5;              // after RACE ... PARTY CRASH

/* 0x00445530(list): the rows drawn. MULTIPLAYER's gets LAN first. */
void MultiplayerRows(lifted::Context& c) {
    if (u32(c.r[4]) != kModeList) {
        F_00445530(c);
        return;
    }
    LanStopLobby();                 // back from LAN: its lobby closed
    generated::Write32(kModeList + 0x4, kModeLanRow + 1);
    generated::Write32(kModeList + 0x10 + 4 * kModeLanRow, kLanString);
    generated::Write8(kModeList + 0x38 + kModeLanRow, 0);
    generated::Write8(kModeList + 0x42 + kModeLanRow, 0);
    F_00445530(c);
}

/* LAN taken: what the main menu does for ONLINE (0x0030E340, its row 3):
   the profile's name copied out, 0x01E6549A set, the main mode (0x01E900B4)
   3, and the online flow's first screen (0x94414018F847A2EA) brought up by
   the screen manager (0x01EA2970: find, then +0x18 and +0x1C). */
void MultiplayerScreen(lifted::Context& c) {
    const u32 screen = u32(c.r[4]), message = u32(c.r[5]), event = u32(c.r[7]);
    if (message != 5 || event != 0 || s8(generated::ReadU8(screen + 0x10)) != 1 ||
        generated::ReadU32(kModeList + 0x8) != kModeLanRow) {
        F_003C9BF0(c);
        return;
    }
    GuestCall(c, 0x0022D180u, {0x004F50C0u, 0, u32(s32(s8(c.r[8])))});
    if (const u32 profile = generated::ReadU32(0x0051BF9Cu)) {
        const u32 name = GuestCall(c, 0x0013B730u, {profile});
        GuestCall(c, 0x002139A0u, {0x00516DF4u, 9, name});
    }
    // The lobby is found or served later, once the network is chosen in the
    // list of network configurations (lan.cpp, ChooseNetwork).
    LanPresetLogin();
    generated::Write8(0x01E6549Au, 1);
    generated::Write32(0x01E900B4u, 3);
    constexpr u32 kManager = 0x01EA2970u;
    // The screen's id is 64 bits, in a1 whole: called by hand, not GuestCall.
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    c.r[4] = kManager;
    c.r[5] = 0x94414018F847A2EAull;
    c.rh[5] = 0;
    lifted::Call(c, 0x00217690u, u32(saved[31]));
    const u32 next = u32(c.r[2]);
    std::memcpy(c.r, saved, sizeof saved);
    if (next && next != generated::ReadU32(kManager + 4)) {
        const u32 vtable = generated::ReadU32(kManager);
        GuestCall(c, generated::ReadU32(vtable + 0x18), {kManager, next, 0});
        generated::Write32(kManager + 4, next);
        if (const u32 from = generated::ReadU32(kManager + 0x10)) {
            const u32 when = generated::ReadU32(from + 0x20);
            generated::Write32(kManager + 0x14, when);
            generated::Write32(next + 0x8, when);
        }
        GuestCall(c, generated::ReadU32(generated::ReadU32(kManager) + 0x1C), {kManager, next, 0});
    }
    std::fprintf(stderr, "[lan] MULTIPLAYER: LAN -> the online flow\n");
}

}  // namespace

void RegisterGameIopServers();     // iop_servers.cpp
void RegisterWidescreen();         // widescreen.cpp
void RegisterMouse();              // mouse.cpp
void RegisterControls();           // controls.cpp
void RegisterLan();                // lan.cpp
void RegisterSixtyFps();           // sixty_fps.cpp

void RegisterGameOverrides() {
    // VU1's fast mode: every scripted run draws every picture as the exact
    // VU does (regress.py --vu-fast), for ~15% less of VIF1's worker - the
    // busiest thread on a slow machine.
    ps2host::SetVuFastDefault(true);
    // The GPU pipelines the game draws with, made in the background from the
    // start: none made in the middle of a player's first race (up to 0.7 s
    // each on an integrated GPU's cold driver).
    ps2host::SetGpuPipelineHints(kGpuPipelineHints, sizeof kGpuPipelineHints / sizeof kGpuPipelineHints[0]);
    RegisterGameIopServers();
    RegisterWidescreen();
    RegisterMouse();
    RegisterControls();
    RegisterLan();
    RegisterSixtyFps();
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
    ps2host::Override(0x002FA9A0u, PostEffects, "the frame's speed blur and glow: BLUR (PC SETTINGS)");
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
    ps2host::Override(0x001313C0u, SkipMovie, "a movie screen's update: Start skips the movie");
    ps2host::Override(0x0019A950u, FastLoading<F_0019A950>, "a race's loading: fast-forwarded while it waits");
    ps2host::Override(0x001322B0u, FastLoading<F_001322B0>, "the game's loading: fast-forwarded while it waits");
    ps2host::Override(0x00271A00u, GetString, "the string lookup: PC SETTINGS' words");
    ps2host::Override(0x003181E0u, MainMenuInput, "the main menu's input: PC SETTINGS, QUIT GAME");
    if (LanEnabled()) {
        ps2host::Override(0x00445530u, MultiplayerRows, "LAN: MULTIPLAYER's rows, LAN added");
        ps2host::Override(0x003C9BF0u, MultiplayerScreen, "LAN: MULTIPLAYER's LAN opens the online flow");
    }
    ps2host::Override(0x00318100u, MainMenu, "the main menu: QUIT GAME's message box");
    ps2host::Override(0x00321470u, MenuList, "a list: the main menu's with QUIT GAME");
    ps2host::Override(0x0031C350u, MenuHelps, "a ticker: the main menu's descriptions with QUIT GAME's");
    ps2host::Override(0x00319360u, SelectorMove<F_00319360, -1>, "a selector's Up: PC SETTINGS' pages scroll");
    ps2host::Override(0x00319330u, SelectorMove<F_00319330, 1>, "a selector's Down: PC SETTINGS' pages scroll");
}
