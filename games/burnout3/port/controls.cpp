/*
 * The player's controls (PC SETTINGS > CONTROLS): which key or controller
 * button does what - the keyboard's and the controller's each, and each two
 * ways: racing, and in the menus. Racing, Cross accelerates; in a menu it
 * chooses - so a player may accelerate with a trigger and still choose with
 * Cross, or steer with A and D and still move through menus with the arrows.
 *
 * Underneath, the game reads a DualShock 2. Each binding says which key or
 * button holds one of its inputs, in one context: the keys are read through
 * the host's keyboard mapper, the controller's buttons through its filter
 * (both once a pad read), and the game sees the DualShock 2 they make.
 * Racing is the race's world and HUD going on with no menu up; anything
 * else - the frontend, the pause screen, a message box - is the menus.
 *
 * The race's controls (found by holding each button through the first
 * race): Cross accelerates, Square brakes and reverses - both read for how
 * far they are pressed - R1 boosts (and, held in a crash, is Impact Time),
 * Triangle changes the camera, L1 looks back, L2 skips to the next EA Trax
 * song, Start pauses; Circle and R2 do nothing seen. The controller's
 * layouts: CLASSIC, the game's own, and MODERN, as racing games have them
 * now -
 *
 *   R2 (how far)   accelerate        Cross     boost
 *   L2 (how far)   brake, reverse    Square    look back
 *   L1             next song         Triangle  camera
 *
 * - or the player's own. The race reads how far Cross and Square are
 * pressed, in a narrow band of the DualShock 2's pressure (measured with
 * --press cross@...:PRESSURE): 30 of 255 coasts, 38 holds about 58 mph, 44
 * about 68, 50 and more is full throttle. A trigger's travel is spread over
 * that band, so the whole pull is the throttle, not its first fifth.
 *
 * With TRIGGER EFFECTS, a DualSense's triggers push back while racing, when
 * they accelerate and brake: the accelerator lightly, the brake harder from
 * a little way in.
 *
 * Binding: the row's input waits for the next key or button pressed on its
 * device - once whatever was held is let go - and takes it; an action that
 * had it gets the row's old one, so each key does one thing per context.
 * Escape, a right click or five seconds leave it be. While a binding waits,
 * and until what bound it is let go, the game sees nothing.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "controls.h"
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

bool MenuShowing();                         // mouse.cpp
unsigned long long RaceWorldCalls();        // widescreen.cpp
void F_001A0790(lifted::Context& c);        // the race's HUD

namespace controls {
namespace {

// DualShock 2 inputs (libpad's order).
constexpr int kSelect = 0, kL3 = 1, kR3 = 2, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8,
              kR2 = 9, kL1 = 10, kR1 = 11, kTriangle = 12, kCircle = 13, kCross = 14, kSquare = 15;
constexpr int kInputs = 16;
constexpr u16 Bit(int input) { return u16(1u << input); }
// The settings' names for them.
const char* const kInputKeys[kInputs] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                         "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};

// A trigger counts from this far in (of 255): its rest, and a little play.
constexpr u8 kTriggerDeadZone = 12;
// The race's band of pressure (see above), and the pull that is all of it.
constexpr int kBandLow = 24, kBandHigh = 56, kFullPull = 245;

/* A trigger's travel as the pressure the race reads. */
u8 Pressure(u8 travel) {
    if (travel >= kFullPull) return 0xFF;
    const int t = travel - kTriggerDeadZone, span = kFullPull - kTriggerDeadZone;
    return u8(kBandLow + (kBandHigh - kBandLow) * t / span);
}

const bool kLog = std::getenv("PS2HOST_CONTROLS_LOG") != nullptr;
// PS2HOST_MODERN_CONTROLS=1: the controller's MODERN racing layout, whatever
// the settings say (a scripted run's).
const bool kForcedModern = std::getenv("PS2HOST_MODERN_CONTROLS") != nullptr;

// ------------------------------------------------------------ the keys

/* The keys a binding may use, by virtual key: the name a row shows (the
   game's font has no '-' or '/': words for them). F1, F9, F10, F12 and Tab
   are the port's own (the FPS counter, the PC settings panel, the HUD
   editor, a capture, the texture pack), Escape cancels a binding, Alt+Enter
   is fullscreen - none can be bound. */
struct Key {
    int vk;
    const char* name;
};
const Key kKeys[] = {
    {'A', "A"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "E"}, {'F', "F"}, {'G', "G"}, {'H', "H"}, {'I', "I"},
    {'J', "J"}, {'K', "K"}, {'L', "L"}, {'M', "M"}, {'N', "N"}, {'O', "O"}, {'P', "P"}, {'Q', "Q"}, {'R', "R"},
    {'S', "S"}, {'T', "T"}, {'U', "U"}, {'V', "V"}, {'W', "W"}, {'X', "X"}, {'Y', "Y"}, {'Z', "Z"},
    {'0', "0"}, {'1', "1"}, {'2', "2"}, {'3', "3"}, {'4', "4"}, {'5', "5"}, {'6', "6"}, {'7', "7"}, {'8', "8"},
    {'9', "9"},
    {VK_UP, "UP ARROW"}, {VK_DOWN, "DOWN ARROW"}, {VK_LEFT, "LEFT ARROW"}, {VK_RIGHT, "RIGHT ARROW"},
    {VK_SPACE, "SPACE"}, {VK_RETURN, "ENTER"}, {VK_BACK, "BACKSPACE"},
    {VK_LSHIFT, "LEFT SHIFT"}, {VK_RSHIFT, "RIGHT SHIFT"}, {VK_LCONTROL, "LEFT CTRL"}, {VK_RCONTROL, "RIGHT CTRL"},
    {VK_LMENU, "LEFT ALT"}, {VK_RMENU, "RIGHT ALT"}, {VK_CAPITAL, "CAPS LOCK"},
    {VK_INSERT, "INSERT"}, {VK_DELETE, "DELETE"}, {VK_HOME, "HOME"}, {VK_END, "END"}, {VK_PRIOR, "PAGE UP"},
    {VK_NEXT, "PAGE DOWN"},
    {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"},
    {VK_F11, "F11"},
    {VK_NUMPAD0, "NUM 0"}, {VK_NUMPAD1, "NUM 1"}, {VK_NUMPAD2, "NUM 2"}, {VK_NUMPAD3, "NUM 3"},
    {VK_NUMPAD4, "NUM 4"}, {VK_NUMPAD5, "NUM 5"}, {VK_NUMPAD6, "NUM 6"}, {VK_NUMPAD7, "NUM 7"},
    {VK_NUMPAD8, "NUM 8"}, {VK_NUMPAD9, "NUM 9"}, {VK_DECIMAL, "NUM POINT"}, {VK_ADD, "NUM PLUS"},
    {VK_SUBTRACT, "NUM MINUS"}, {VK_MULTIPLY, "NUM TIMES"}, {VK_DIVIDE, "NUM DIVIDE"},
    {VK_OEM_1, "SEMICOLON"}, {VK_OEM_PLUS, "EQUALS"}, {VK_OEM_COMMA, "COMMA"}, {VK_OEM_MINUS, "MINUS"},
    {VK_OEM_PERIOD, "PERIOD"}, {VK_OEM_2, "SLASH"}, {VK_OEM_3, "BACKQUOTE"}, {VK_OEM_4, "LEFT BRACKET"},
    {VK_OEM_5, "BACKSLASH"}, {VK_OEM_6, "RIGHT BRACKET"}, {VK_OEM_7, "APOSTROPHE"},
};

const char* KeyName(int vk) {
    for (const Key& k : kKeys)
        if (k.vk == vk) return k.name;
    return nullptr;
}

int KeyByName(const std::string& name) {
    for (const Key& k : kKeys)
        if (name == k.name) return k.vk;
    return 0;
}

// The controller's buttons by their DualShock 2 place, by style.
const char* const kPlayStationNames[kInputs] = {"SELECT", "L3", "R3", "START", "DPAD UP", "DPAD RIGHT",
                                                 "DPAD DOWN", "DPAD LEFT", "L2", "R2", "L1", "R1",
                                                 "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"};
const char* const kXboxNames[kInputs] = {"VIEW", "LS", "RS", "MENU", "DPAD UP", "DPAD RIGHT", "DPAD DOWN",
                                         "DPAD LEFT", "LT", "RT", "LB", "RB", "Y", "B", "A", "X"};

int ButtonByName(const std::string& name) {
    for (int i = 0; i < kInputs; ++i)
        if (name == kPlayStationNames[i]) return i;
    return -1;
}

// ------------------------------------------------------------ the bindings

/* One context's bindings: for each DualShock 2 input, what holds it - a
   virtual key (0 none) or a controller button by its place (-1 none). */
struct Bindings {
    int physical[kInputs];
};

// The keyboard's defaults, racing and in the menus: the port's keys since
// its first release.
constexpr Bindings kKeyboardDefaults = {{VK_BACK, 0, 0, VK_RETURN, VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT, '1', '3', 'Q',
                                         'E', 'V', 'C', 'X', 'Z'}};
constexpr Bindings kControllerAsGame = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
// MODERN: accelerate R2, brake L2, boost Cross, look back Square, next song
// L1 - and R1 (on the race's R2, which does nothing).
constexpr Bindings kControllerModern = {{kSelect, kL3, kR3, kStart, kUp, kRight, kDown, kLeft, kL1, kR1, kSquare,
                                         kCross, kTriangle, kCircle, kR2, kL2}};

bool Same(const Bindings& a, const Bindings& b) { return std::memcmp(a.physical, b.physical, sizeof a.physical) == 0; }

struct State {
    bool loaded = false;
    Bindings bindings[2][2];        // [Device][Context]
    unsigned generation = 1;
};
State g_state;

Bindings& Of(Device device, Context context) { return g_state.bindings[int(device)][int(context)]; }

const char* DeviceKey(Device device) { return device == Device::kKeyboard ? "keyboard" : "controller"; }
const char* ContextKey(Context context) { return context == Context::kRacing ? "racing" : "menus"; }

std::string SettingKey(Device device, Context context, int input) {
    return std::string("bind.") + DeviceKey(device) + "." + ContextKey(context) + "." + kInputKeys[input];
}

Bindings Defaults(Device device, Context context) {
    if (device == Device::kKeyboard) return kKeyboardDefaults;
    if (context == Context::kRacing && (kForcedModern || ps2host::SettingOn("modern_controls", false)))
        return kControllerModern;           // the MODERN CONTROLS setting of 0.3.0 and on
    return kControllerAsGame;
}

/* The settings' bindings, each context's on its defaults - what a setting
   names as nothing (or not at all) keeps its default. */
void Load() {
    if (g_state.loaded) return;
    g_state.loaded = true;
    for (Device device : {Device::kKeyboard, Device::kController})
        for (Context context : {Context::kRacing, Context::kMenus}) {
            Bindings b = Defaults(device, context);
            const bool forced = kForcedModern && device == Device::kController && context == Context::kRacing;
            for (int i = 0; i < kInputs && !forced; ++i) {
                const std::string value = ps2host::Setting(SettingKey(device, context, i));
                if (value.empty()) continue;
                if (value == "NONE") b.physical[i] = device == Device::kKeyboard ? 0 : -1;
                else if (device == Device::kKeyboard && KeyByName(value)) b.physical[i] = KeyByName(value);
                else if (device == Device::kController && ButtonByName(value) >= 0)
                    b.physical[i] = ButtonByName(value);
                else continue;
                if (kLog) std::fprintf(stderr, "[controls] %s %s %s: %s (the settings')\n", DeviceKey(device),
                                       ContextKey(context), kInputKeys[i], value.c_str());
            }
            Of(device, context) = b;
        }
}

void Save(Device device, Context context) {
    const Bindings& b = Of(device, context);
    for (int i = 0; i < kInputs; ++i) {
        const int p = b.physical[i];
        const char* name = device == Device::kKeyboard ? (p ? KeyName(p) : nullptr) : (p >= 0 ? kPlayStationNames[p] : nullptr);
        ps2host::SetSetting(SettingKey(device, context, i), name ? name : "NONE");
    }
    ++g_state.generation;
}

/* `input` bound to `physical`: an input that had it gets `input`'s old one. */
void Bind(Device device, Context context, int input, int physical) {
    Bindings& b = Of(device, context);
    for (int i = 0; i < kInputs; ++i)
        if (i != input && b.physical[i] == physical) b.physical[i] = b.physical[input];
    b.physical[input] = physical;
    Save(device, context);
}

// ------------------------------------------------------------ racing

bool g_racing = false;          // ControlsTick's: the racing bindings hold
u64 g_tick = 0;                 // pad reads
u64 g_controller_seen = 0;      // the last pad read a controller was there in
ps2host::ControllerFamily g_family = ps2host::ControllerFamily::kOther;

/* The race's HUD drawn (0x001A0790: only in a race - the menus' and the
   intro's backdrops run the race's world too, without it). */
unsigned long long g_hud_calls = 0;
void RaceHud(lifted::Context& c) {
    ++g_hud_calls;
    F_001A0790(c);
}

/* Racing: the race's world went on, and its HUD was drawn, in the last few
   pad reads (both stop while the game is paused), and no menu shows. */
bool Racing() {
    static unsigned long long world_seen = 0, hud_seen = 0;
    static int world_still = 99, hud_still = 99;
    const unsigned long long world = RaceWorldCalls(), hud = g_hud_calls;
    world_still = world != world_seen ? 0 : world_still + 1;
    hud_still = hud != hud_seen ? 0 : hud_still + 1;
    world_seen = world;
    hud_seen = hud;
    return world_still <= 8 && hud_still <= 8 && !MenuShowing();
}

Context Now() { return g_racing ? Context::kRacing : Context::kMenus; }

// ------------------------------------------------------------ binding

struct Waiting {
    bool active = false;
    Device device = Device::kKeyboard;
    Context context = Context::kMenus;
    int input = 0;
    bool armed[2] = {false, false};     // each device: all it held when it began let go
    u64 until = 0;                      // the pad read it gives up at
    CaptureState ended = CaptureState::kNone;
};
Waiting g_capture;
// After one: the device's input not given to the game until all is let go.
bool g_draining[2] = {false, false};
constexpr u64 kCaptureReads = 5 * 60;   // five seconds of pad reads

void EndCapture(CaptureState how) {
    if (kLog) std::fprintf(stderr, "[controls] binding %s\n", how == CaptureState::kBound ? "made" : "left");
    g_capture.active = false;
    g_capture.ended = how;
    g_draining[0] = g_draining[1] = true;
}

bool AnyKeyDown() {
    for (const Key& k : kKeys)
        if (ps2host::HostKeyDown(k.vk)) return true;
    return ps2host::HostKeyDown(VK_ESCAPE);
}

bool Focused() { return ps2host::WindowFocused() || ps2host::KeysScripted(); }

/* The keyboard's DualShock 2, each pad read (the host's keyboard mapper). */
u16 Keyboard() {
    Load();
    if (!Focused()) return 0;
    if (g_capture.active) {
        if (!g_capture.armed[0]) {
            g_capture.armed[0] = !AnyKeyDown();
        } else if (ps2host::HostKeyDown(VK_ESCAPE)) {
            EndCapture(CaptureState::kCancelled);
        } else if (g_capture.device == Device::kKeyboard) {
            for (const Key& k : kKeys)
                if (ps2host::HostKeyDown(k.vk)) {
                    if (kLog) std::fprintf(stderr, "[controls] %s %s: %s\n", DeviceKey(g_capture.device),
                                           kInputKeys[g_capture.input], k.name);
                    Bind(g_capture.device, g_capture.context, g_capture.input, k.vk);
                    EndCapture(CaptureState::kBound);
                    break;
                }
        }
        return 0;
    }
    const Bindings& b = Of(Device::kKeyboard, Now());
    u16 out = 0;
    for (int i = 0; i < kInputs; ++i)
        if (b.physical[i] && ps2host::HostKeyDown(b.physical[i])) out |= Bit(i);
    if (g_draining[0]) {
        if (out || ps2host::HostKeyDown(VK_ESCAPE)) return 0;
        g_draining[0] = false;
    }
    return out;
}

/* The controller's DualShock 2, each pad read it is there (the host's
   controller filter): its buttons as the bindings have them. */
void Controller(ps2host::HostPadState* pad) {
    Load();
    g_controller_seen = g_tick;
    g_family = pad->family;
    if (g_capture.active) {
        // Whatever is pressed is the binding's, not the game's.
        const u16 held = u16(pad->buttons | (pad->l2 > kTriggerDeadZone ? Bit(kL2) : 0) |
                             (pad->r2 > kTriggerDeadZone ? Bit(kR2) : 0));
        if (!g_capture.armed[1]) {
            g_capture.armed[1] = held == 0;
        } else if (g_capture.device == Device::kController && held) {
            int button = 0;
            while (!(held & Bit(button))) ++button;
            if (kLog) std::fprintf(stderr, "[controls] controller %s: %s\n", kInputKeys[g_capture.input],
                                   kPlayStationNames[button]);
            Bind(g_capture.device, g_capture.context, g_capture.input, button);
            EndCapture(CaptureState::kBound);
        }
        *pad = ps2host::HostPadState{};
        return;
    }
    const bool racing = g_racing;
    const Bindings& b = Of(Device::kController, Now());
    const u16 held = pad->buttons;
    u16 out = 0;
    u8 pressure[kInputs] = {};
    for (int i = 0; i < kInputs; ++i) {
        const int p = b.physical[i];
        if (p < 0) continue;
        // Racing, the throttle and the brake on a trigger: how far it is pulled.
        if (racing && (i == kCross || i == kSquare) && (p == kL2 || p == kR2)) {
            const u8 travel = p == kL2 ? pad->l2 : pad->r2;
            if (travel > kTriggerDeadZone) {
                out |= Bit(i);
                pressure[i] = Pressure(travel);
            }
        } else if (held & Bit(p)) {
            out |= Bit(i);
            pressure[i] = 0xFF;
        }
    }
    if (g_draining[1]) {
        if (out) {
            out = 0;
            std::fill(std::begin(pressure), std::end(pressure), u8(0));
        } else {
            g_draining[1] = false;
        }
    }
    pad->buttons = out;
    for (int i = 0; i < kInputs; ++i) pad->pressure[i] = pressure[i];
}

}  // namespace

// ------------------------------------------------------------ the rows

namespace {

/* The pages' rows (null name: the input's own button, by the prompts'
   style - the menus' buttons the game gives no single meaning). */
const Action kKeyboardRacing[] = {
    {kCross, "ACCELERATE", "THE KEY HELD TO DRIVE"},
    {kSquare, "BRAKE AND REVERSE", "HELD TO SLOW DOWN, AND TO REVERSE WHEN STOPPED"},
    {kR1, "BOOST", "BOOST, AND IMPACT TIME WHEN HELD IN A CRASH"},
    {kLeft, "STEER LEFT", "STEERS, AND MOVES YOUR WRECK IN AFTERTOUCH"},
    {kRight, "STEER RIGHT", "STEERS, AND MOVES YOUR WRECK IN AFTERTOUCH"},
    {kTriangle, "CHANGE CAMERA", "THE VIEW FROM BEHIND, OR FROM THE BUMPER"},
    {kL1, "LOOK BACK", "HELD TO SEE WHAT IS BEHIND YOU"},
    {kL2, "NEXT SONG", "SKIPS TO THE NEXT EA TRAX SONG"},
    {kStart, "PAUSE", "THE PAUSE MENU"},
    {kCircle, "HORN", "HONKS, WITH THE HORN MOD ON (PC SETTINGS, MODS)"},
    {kR2, "CRASH BREAKER", "IN EVERY MODE WITH ITS MOD ON (PC SETTINGS, MODS)"},
};
const Action kControllerRacing[] = {
    {kCross, "ACCELERATE", "ON A TRIGGER: HOW FAR IT IS PULLED IS HOW FAST"},
    {kSquare, "BRAKE AND REVERSE", "ON A TRIGGER: HOW FAR IT IS PULLED IS HOW HARD"},
    {kR1, "BOOST", "BOOST, AND IMPACT TIME WHEN HELD IN A CRASH"},
    {kTriangle, "CHANGE CAMERA", "THE VIEW FROM BEHIND, OR FROM THE BUMPER"},
    {kL1, "LOOK BACK", "HELD TO SEE WHAT IS BEHIND YOU"},
    {kL2, "NEXT SONG", "SKIPS TO THE NEXT EA TRAX SONG"},
    {kStart, "PAUSE", "THE PAUSE MENU"},
    {kCircle, "HORN", "HONKS, WITH THE HORN MOD ON (PC SETTINGS, MODS)"},
    {kR2, "CRASH BREAKER", "IN EVERY MODE WITH ITS MOD ON (PC SETTINGS, MODS)"},
};
const Action kKeyboardMenus[] = {
    {kCross, "ACCEPT", "CHOOSES WHAT IS HIGHLIGHTED"},
    {kTriangle, "BACK", "GOES BACK A SCREEN"},
    {kUp, "UP", "MOVES THE HIGHLIGHT"},
    {kDown, "DOWN", "MOVES THE HIGHLIGHT"},
    {kLeft, "LEFT", "MOVES THE HIGHLIGHT, CHANGES A CHOICE"},
    {kRight, "RIGHT", "MOVES THE HIGHLIGHT, CHANGES A CHOICE"},
    {kCircle, "DETAILS", "DRIVER DETAILS, A CAR'S COLOR"},
    {kStart, "START", "THE TITLE SCREEN, AND RESUMING A RACE"},
    {kSquare, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kL1, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kR1, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kL2, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kR2, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kSelect, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
};
const Action kControllerMenus[] = {
    {kCross, "ACCEPT", "CHOOSES WHAT IS HIGHLIGHTED"},
    {kTriangle, "BACK", "GOES BACK A SCREEN"},
    {kCircle, "DETAILS", "DRIVER DETAILS, A CAR'S COLOR"},
    {kStart, "START", "THE TITLE SCREEN, AND RESUMING A RACE"},
    {kSquare, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kL1, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kR1, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kL2, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kR2, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
    {kSelect, nullptr, "WHERE THE MENUS SHOW THIS BUTTON"},
};

template <std::size_t N>
constexpr int Count(const Action (&)[N]) { return int(N); }

const Action* Rows(Device device, Context context, int* count) {
    if (device == Device::kKeyboard) {
        *count = context == Context::kRacing ? Count(kKeyboardRacing) : Count(kKeyboardMenus);
        return context == Context::kRacing ? kKeyboardRacing : kKeyboardMenus;
    }
    *count = context == Context::kRacing ? Count(kControllerRacing) : Count(kControllerMenus);
    return context == Context::kRacing ? kControllerRacing : kControllerMenus;
}

const char* ButtonName(int button) {
    if (button < 0 || button >= kInputs) return "NONE";
    return PromptStyle() == Style::kXbox ? kXboxNames[button] : kPlayStationNames[button];
}

}  // namespace

int ActionCount(Device device, Context context) {
    int count = 0;
    Rows(device, context, &count);
    return count;
}

const Action& ActionAt(Device device, Context context, int row) {
    int count = 0;
    const Action* rows = Rows(device, context, &count);
    return rows[std::clamp(row, 0, count - 1)];
}

std::string ActionName(Device device, Context context, int row) {
    const Action& a = ActionAt(device, context, row);
    if (a.name) return a.name;
    return std::string(ButtonName(a.input)) + " BUTTON";
}

std::string BoundName(Device device, Context context, int input) {
    Load();
    const int p = Of(device, context).physical[input];
    if (device == Device::kKeyboard) {
        const char* name = p ? KeyName(p) : nullptr;
        return name ? name : "NONE";
    }
    return ButtonName(p);
}

void ResetDefaults(Device device, Context context) {
    Load();
    if (device == Device::kController && context == Context::kRacing) {
        SetRacingLayout(Layout::kClassic);
        return;
    }
    Of(device, context) = Defaults(device, context);
    Save(device, context);
}

Layout RacingLayout() {
    Load();
    const Bindings& b = Of(Device::kController, Context::kRacing);
    return Same(b, kControllerAsGame) ? Layout::kClassic : Same(b, kControllerModern) ? Layout::kModern : Layout::kCustom;
}

void SetRacingLayout(Layout layout) {
    Load();
    if (layout == Layout::kCustom) return;
    Of(Device::kController, Context::kRacing) = layout == Layout::kModern ? kControllerModern : kControllerAsGame;
    Save(Device::kController, Context::kRacing);
    // The bindings say it now; the old setting with them, for a downgrade.
    ps2host::SetSettingOn("modern_controls", layout == Layout::kModern);
}

void BeginCapture(Device device, Context context, int input) {
    Load();
    g_capture = Waiting{};
    g_capture.active = true;
    g_capture.device = device;
    g_capture.context = context;
    g_capture.input = input;
    g_capture.until = g_tick + kCaptureReads;
    if (kLog) std::fprintf(stderr, "[controls] binding %s %s %s\n", DeviceKey(device), ContextKey(context), kInputKeys[input]);
}

CaptureState Capture(int* seconds) {
    if (g_capture.active) {
        if (seconds) *seconds = int((g_capture.until - std::min(g_tick, g_capture.until) + 59) / 60);
        return CaptureState::kWaiting;
    }
    const CaptureState ended = g_capture.ended;
    g_capture.ended = CaptureState::kNone;
    return ended;
}

bool Capturing() { return g_capture.active; }

/* A right click (mouse.cpp) leaves a binding as it was. */
void CancelCapture() {
    if (g_capture.active) EndCapture(CaptureState::kCancelled);
}

Style PromptStyle() {
    const std::string want = ps2host::Setting("button_prompts", "auto");
    if (want == "xbox") return Style::kXbox;
    if (want == "playstation") return Style::kPlayStation;
    return g_family == ps2host::ControllerFamily::kXbox ? Style::kXbox : Style::kPlayStation;
}

bool ControllerPresent() { return g_controller_seen && g_controller_seen + 2 >= g_tick; }

int MenuBinding(Device device, int input) {
    Load();
    return Of(device, Context::kMenus).physical[input];
}

int RacingBinding(Device device, int input) {
    Load();
    return Of(device, Context::kRacing).physical[input];
}

bool MenuBindingsAsGame(Device device) {
    Load();
    return device == Device::kController ? Same(Of(device, Context::kMenus), kControllerAsGame)
                                         : Same(Of(device, Context::kMenus), kKeyboardDefaults);
}

std::string ButtonText(const std::string& text, bool controller, int* slot) {
    // The language file's every string naming a button (all of them: the
    // file has these four), and which binding it means.
    static const struct {
        const char* game;
        const char* before;
        const char* after;
        Context context;
        int input;
        bool title;             // Mixed case ("Press START button"), the key's word too
    } kTexts[] = {
        {"PRESS R1 BUTTON TO BOOST", "PRESS ", " TO BOOST", Context::kRacing, kR1, false},
        {"WHEN CRASHING HOLD THE R1 BUTTON FOR AFTERTOUCH", "WHEN CRASHING HOLD THE ", " FOR AFTERTOUCH",
         Context::kRacing, kR1, false},
        {"Press START button to continue", "Press ", " to continue", Context::kMenus, kStart, true},
        {"PRESS START BUTTON TO START GAME", "PRESS ", " TO START GAME", Context::kMenus, kStart, false},
    };
    for (int i = 0; i < int(sizeof kTexts / sizeof kTexts[0]); ++i) {
        const auto& t = kTexts[i];
        if (text != t.game) continue;
        const Device device = controller ? Device::kController : Device::kKeyboard;
        const char* word = controller ? (t.title ? " button" : " BUTTON") : (t.title ? " key" : " KEY");
        *slot = i;
        return t.before + BoundName(device, t.context, t.input) + word + t.after;
    }
    return "";
}

unsigned Generation() { return g_state.generation; }

}  // namespace controls

/* Once a pad read (mouse.cpp's MouseTick, after the keyboard and the
   controller are read): whether the racing bindings hold, a binding's time
   running out, and the triggers' push. */
void ControlsTick() {
    using namespace controls;
    ++g_tick;
    const bool racing = Racing();
    if (kLog && racing != g_racing) std::fprintf(stderr, "[controls] %s\n", racing ? "racing" : "menus");
    g_racing = racing;
    // Splitscreen (overrides.cpp's PLAYERS): each player's device drives
    // their own car while racing; in the menus, the pause's too, every
    // device is player 1's as before.
    ps2host::SetPortsSplit(racing);
    if (g_capture.active && g_tick >= g_capture.until) EndCapture(CaptureState::kCancelled);
    // The triggers' push, while racing with them (a DualSense's).
    ps2host::HostTriggerEffect left, right;
    if (racing && ps2host::SettingOn("trigger_effects", true)) {
        const Bindings& b = Of(Device::kController, Context::kRacing);
        auto push = [&](int input, u8 start, u8 force) {
            const int p = b.physical[input];
            if (p != kL2 && p != kR2) return;
            ps2host::HostTriggerEffect& e = p == kL2 ? left : right;
            e.kind = ps2host::HostTriggerEffect::kResistance;
            e.start = start;
            e.force = force;
        };
        push(kCross, 0, 40);            // the accelerator, lightly
        push(kSquare, 50, 170);         // the brake, harder from a little way in
    }
    ps2host::HostTriggerEffects(left, right);
}

void RegisterControls() {
    ps2host::SetKeyboardMapper(controls::Keyboard);
    ps2host::SetControllerFilter(controls::Controller);
    ps2host::Override(0x001A0790u, controls::RaceHud, "the race's HUD: racing, for the player's racing controls");
}
