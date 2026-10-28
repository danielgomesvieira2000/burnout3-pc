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
 * - or the player's own. Both honk on L3 (the horn patch's Circle). In the
 * menus MODERN's Circle goes back (and Triangle shows details). The race reads how far Cross and Square are
 * pressed, in a narrow band of the DualShock 2's pressure (measured with
 * --press cross@...:PRESSURE): 30 of 255 coasts, 38 holds about 58 mph, 44
 * about 68, 50 and more is full throttle. A trigger's travel is spread over
 * that band, so the whole pull is the throttle, not its first fifth.
 *
 * With TRIGGER EFFECTS, a DualSense's triggers push back while racing, when
 * they accelerate and brake, as the car does (triggers.cpp).
 *
 * Binding: the row's input waits for the next key or button pressed on its
 * device - once whatever was held is let go - and takes it; an action that
 * had it gets the row's old one, so each key does one thing per context.
 * Escape, a right click or five seconds leave it be. While a binding waits,
 * and until what bound it is let go, the game sees nothing.
 */

#include "ps2host/platform.h"      // the keys' names

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "controls.h"
#include "triggers.h"
#include "ps2host/bindings.h"
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

bool MenuShowing();                         // mouse.cpp
unsigned long long RaceWorldCalls();        // widescreen.cpp
void F_001A0790(lifted::Context& c);        // the race's HUD

void OrbitStick(ps2host::HostPadState* pad, bool racing);     // orbit_camera.cpp

namespace controls {
namespace {

// DualShock 2 inputs (libpad's order), and the bindings' tables, keys and
// buttons: the engine's (bindings.h).
namespace bindings = ps2host::bindings;
using bindings::Bit;
using bindings::kInputs;
using bindings::kSelect;
using bindings::kL3;
using bindings::kR3;
using bindings::kStart;
using bindings::kUp;
using bindings::kRight;
using bindings::kDown;
using bindings::kLeft;
using bindings::kL2;
using bindings::kR2;
using bindings::kL1;
using bindings::kR1;
using bindings::kTriangle;
using bindings::kCircle;
using bindings::kCross;
using bindings::kSquare;

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
//
// The keys a binding may use (F1, F9, F10, F12 and Tab are the port's own,
// Escape cancels a binding, Alt+Enter is fullscreen - none can be bound) and
// the controller's buttons' names, by style: the engine's (bindings.h).

// ------------------------------------------------------------ the bindings

/* One context's bindings: for each DualShock 2 input, what holds it - a
   virtual key (0 none) or a controller button by its place (-1 none). */
using Bindings = bindings::Table;

// The keyboard's defaults, racing and in the menus: the port's keys since
// its first release.
constexpr Bindings kKeyboardDefaults = {{VK_BACK, 0, 0, VK_RETURN, VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT, '1', '3', 'Q',
                                         'E', 'V', 'C', 'X', 'Z'}};
constexpr Bindings kControllerAsGame = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
// Racing, both layouts honk on L3 (the left stick pressed: the HORN mod's
// Circle, which the race itself does not use; the race's L3, nothing seen,
// on Circle).
// CLASSIC: the game's own.
constexpr Bindings kControllerClassic = {{0, kCircle, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, kL3, 14, 15}};
// MODERN: accelerate R2, brake L2, boost Cross, look back Square, next song
// L1 - and R1 (on the race's R2, which does nothing).
constexpr Bindings kControllerModern = {{kSelect, kCircle, kR3, kStart, kUp, kRight, kDown, kLeft, kL1, kR1, kSquare,
                                         kCross, kTriangle, kL3, kR2, kL2}};
// The layouts before the horn moved (0.6 and before): a racing layout saved
// as one of them is that layout still.
constexpr Bindings kControllerModernBefore = {{kSelect, kL3, kR3, kStart, kUp, kRight, kDown, kLeft, kL1, kR1,
                                               kSquare, kCross, kTriangle, kCircle, kR2, kL2}};

// MODERN's menus: Circle goes back (the game's Triangle), as B does on a PC
// game's controller, and Triangle has Circle's DETAILS.
constexpr Bindings kControllerMenusModern = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, kCircle, kTriangle, 14, 15}};

struct State {
    bool loaded = false;
    Bindings bindings[2][2];        // [Device][Context]
    unsigned generation = 1;
};
State g_state;

Bindings& Of(Device device, Context context) { return g_state.bindings[int(device)][int(context)]; }

const char* DeviceKey(Device device) { return device == Device::kKeyboard ? "keyboard" : "controller"; }
const char* ContextKey(Context context) { return context == Context::kRacing ? "racing" : "menus"; }
bindings::Device Engine(Device device) {
    return device == Device::kKeyboard ? bindings::Device::kKeyboard : bindings::Device::kController;
}

Bindings Defaults(Device device, Context context) {
    if (device == Device::kKeyboard) return kKeyboardDefaults;
    if (context == Context::kRacing && (kForcedModern || ps2host::SettingOn("modern_controls", false)))
        return kControllerModern;           // the MODERN CONTROLS setting of 0.3.0 and on
    if (context == Context::kMenus && ps2host::SettingOn("modern_controls", false)) return kControllerMenusModern;
    return context == Context::kRacing ? kControllerClassic : kControllerAsGame;
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
            if (!forced) b = bindings::Load(Engine(device), ContextKey(context), b, kLog);
            if (device == Device::kController && context == Context::kRacing) {
                if (Same(b, kControllerAsGame)) b = kControllerClassic;
                else if (Same(b, kControllerModernBefore)) b = kControllerModern;
            }
            Of(device, context) = b;
        }
}

void Save(Device device, Context context) {
    bindings::Save(Engine(device), ContextKey(context), Of(device, context));
    ++g_state.generation;
}

/* `input` bound to `physical`: an input that had it gets `input`'s old one. */
void Bind(Device device, Context context, int input, int physical) {
    bindings::Bind(Of(device, context), input, physical);
    Save(device, context);
}

// ------------------------------------------------------------ racing

bool g_racing = false;          // ControlsTick's: the racing bindings hold
u8 g_travel[2] = {};            // L2's and R2's travel, the most any controller had since the last tick
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

bool Focused() { return ps2host::WindowFocused() || ps2host::KeysScripted(); }

/* The keyboard's DualShock 2, each pad read (the host's keyboard mapper). */
u16 Keyboard() {
    Load();
    if (!Focused()) return 0;
    if (g_capture.active) {
        if (!g_capture.armed[0]) {
            g_capture.armed[0] = !bindings::AnyKeyDown();
        } else if (ps2host::HostKeyDown(VK_ESCAPE)) {
            EndCapture(CaptureState::kCancelled);
        } else if (g_capture.device == Device::kKeyboard) {
            for (int vk : bindings::BindableKeys())
                if (ps2host::HostKeyDown(vk)) {
                    if (kLog) std::fprintf(stderr, "[controls] %s %s: %s\n", DeviceKey(g_capture.device),
                                           bindings::InputKey(g_capture.input), bindings::KeyName(vk));
                    Bind(g_capture.device, g_capture.context, g_capture.input, vk);
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

/* GYRO STEERING (a phone's, PORT SETTINGS > CONTROLS): racing, the phone
   turned as a wheel steers - the left stick's X from its tilt, full lock at
   the sensitivity's angle - unless the stick itself is pushed (the touch
   controls' or a controller's). Never in the menus. */
const char* const kGyroLevels[] = {"off", "low", "medium", "high"};
constexpr float kGyroFullLock[] = {0.0f, 40.0f, 28.0f, 18.0f};     // degrees, by level
constexpr float kGyroDeadZone = 2.0f;

void GyroSteer(ps2host::HostPadState* pad, bool racing) {
    if (!racing) return;
    const int level = GyroLevel();
    float degrees = 0.0f;
    if (!level || !ps2host::Tilt(&degrees)) return;
    if (std::abs(int(pad->sticks[2]) - 0x80) > 0x20) return;      // the stick's own
    const float turn = std::abs(degrees) <= kGyroDeadZone ? 0.0f
                       : (degrees - std::copysign(kGyroDeadZone, degrees)) / (kGyroFullLock[level] - kGyroDeadZone);
    pad->sticks[2] = u8(std::lround(127.5f + std::clamp(turn, -1.0f, 1.0f) * 127.5f));
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
            if (kLog) std::fprintf(stderr, "[controls] controller %s: %s\n", bindings::InputKey(g_capture.input),
                                   bindings::ButtonName(button, bindings::Style::kPlayStation));
            Bind(g_capture.device, g_capture.context, g_capture.input, button);
            EndCapture(CaptureState::kBound);
        }
        *pad = ps2host::HostPadState{};
        return;
    }
    g_travel[0] = std::max(g_travel[0], pad->l2);
    g_travel[1] = std::max(g_travel[1], pad->r2);
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
    OrbitStick(pad, racing);    // (the right stick: the camera's, with ORBIT CAMERA on)
    GyroSteer(pad, racing);
}

}  // namespace

int GyroLevel() {
    const std::string level = ps2host::Setting("gyro_steering", "off");
    for (int i = 0; i < int(std::size(kGyroLevels)); ++i)
        if (level == kGyroLevels[i]) return i;
    return 0;
}
void SetGyroLevel(int level) { ps2host::SetSetting("gyro_steering", kGyroLevels[level]); }

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
    {kCircle, "HORN", "HONKS, THE HEADLIGHTS ON WHILE IT IS HELD"},
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
    {kCircle, "HORN", "HONKS, THE HEADLIGHTS ON WHILE IT IS HELD"},
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
    return bindings::ButtonName(button, PromptStyle() == Style::kXbox ? bindings::Style::kXbox
                                                                        : bindings::Style::kPlayStation);
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
        const char* name = p ? bindings::KeyName(p) : nullptr;
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
    return Same(b, kControllerClassic) ? Layout::kClassic : Same(b, kControllerModern) ? Layout::kModern : Layout::kCustom;
}

void SetRacingLayout(Layout layout) {
    Load();
    if (layout == Layout::kCustom) return;
    Of(Device::kController, Context::kRacing) = layout == Layout::kModern ? kControllerModern : kControllerClassic;
    Save(Device::kController, Context::kRacing);
    // The menus' with it, where they are either layout's (the player's own
    // kept).
    Bindings& menus = Of(Device::kController, Context::kMenus);
    if (Same(menus, kControllerAsGame) || Same(menus, kControllerMenusModern)) {
        menus = layout == Layout::kModern ? kControllerMenusModern : kControllerAsGame;
        Save(Device::kController, Context::kMenus);
    }
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
    if (kLog) std::fprintf(stderr, "[controls] binding %s %s %s\n", DeviceKey(device), ContextKey(context), bindings::InputKey(input));
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

namespace presence {
void Tick(bool racing);   // presence.cpp
}

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
    // The triggers' push, while racing (a DualSense's): the car's physics
    // shaping the accelerator's and the brake's (triggers.cpp), each on the
    // trigger it is bound to.
    const Bindings& b = Of(Device::kController, Context::kRacing);
    const int accel_on = b.physical[kCross], brake_on = b.physical[kSquare];
    auto travel = [](int p) {
        return p == kL2 ? g_travel[0] / 255.0f : p == kR2 ? g_travel[1] / 255.0f : 0.0f;
    };
    ps2host::HostTriggerEffect accel, brake, left, right;
    triggers::Tick(racing, travel(accel_on), travel(brake_on), &accel, &brake);
    g_travel[0] = g_travel[1] = 0;
    if (accel_on == kL2) left = accel;
    if (accel_on == kR2) right = accel;
    if (brake_on == kL2) left = brake;
    if (brake_on == kR2) right = brake;
    ps2host::HostTriggerEffects(left, right);
    // Discord's status (presence.cpp): racing, or in the menus.
    presence::Tick(racing);
}

void RegisterControls() {
    ps2host::SetKeyboardMapper(controls::Keyboard);
    ps2host::SetControllerFilter(controls::Controller);
    ps2host::Override(0x001A0790u, controls::RaceHud, "the race's HUD: racing, for the player's racing controls");
}

/* Whether the racing bindings hold (orbit_camera.cpp's: the camera turned only then). */
bool ControlsRacing() { return controls::g_racing; }
