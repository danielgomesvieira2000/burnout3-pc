/*
 * The player's controls (controls.cpp): which key or button does what, for
 * the keyboard and the controller each, racing and in the menus each - set
 * in PC SETTINGS > CONTROLS, kept in the settings file.
 */

#pragma once

#include <string>

namespace controls {

enum class Device { kKeyboard, kController };
enum class Context { kRacing, kMenus };

/* A row of a bindings page: what the game does with one of the DualShock 2's
   inputs (libpad's order: 0 Select ... 15 Square), in that context. */
struct Action {
    int input;
    const char* name;           // as the row says it ("ACCELERATE")
    const char* help;
};
/* The rows of a page (the device's: the keyboard steers with keys, a
   controller with its stick). */
int ActionCount(Device device, Context context);
const Action& ActionAt(Device device, Context context, int row);
/* A row's name as shown: the controller's own buttons named by the prompts' style. */
std::string ActionName(Device device, Context context, int row);

/* What the action's input is bound to, as the player reads it: "LEFT SHIFT",
   "CROSS" or "A" (the prompts' style). */
std::string BoundName(Device device, Context context, int input);

/* The defaults again (the controller's racing: the game's own layout). */
void ResetDefaults(Device device, Context context);
/* The controller's racing layouts: the game's own (CLASSIC), triggers to
   accelerate and brake (MODERN), or one of the player's (CUSTOM). */
enum class Layout { kClassic, kModern, kCustom };
Layout RacingLayout();
void SetRacingLayout(Layout layout);

/* Binding a row: from now, the next key or button the player presses on the
   device (once everything held is let go) is the input's - swapped with the
   action that had it, if one did. Escape, a right click or five seconds
   without one leave it as it was. While it waits the game sees nothing. */
void BeginCapture(Device device, Context context, int input);
enum class CaptureState { kNone, kWaiting, kBound, kCancelled };
/* Where a binding stands; kBound and kCancelled once each, as it ends.
   `seconds` the time left to press one. */
CaptureState Capture(int* seconds);
bool Capturing();
/* Leaves a binding as it was (a right click). */
void CancelCapture();

/* The prompts' style for a controller: "auto" (the controller's own),
   "playstation" or "xbox". */
enum class Style { kPlayStation, kXbox };
Style PromptStyle();
/* Whether a controller is plugged in. */
bool ControllerPresent();

/* The physical input bound to a DualShock 2 input in the menus (the
   prompts'): a virtual key (keyboard), or a controller button by its
   DualShock 2 place (0-15); -1 none. */
int MenuBinding(Device device, int input);
/* The same, racing (the race's own prompts: Impact Time's R1). */
int RacingBinding(Device device, int input);
/* Whether the controller's menus are as the game's own (each button
   itself) - for the prompts, the game's own art then. */
bool MenuBindingsAsGame(Device device);

/* The game's words naming a button (the race's boost hints, the title's
   PRESS START), as the controls have it: the words for `text` (one of the
   game's) and which of them it is (`slot`, 0-3); "" if it is not one. */
std::string ButtonText(const std::string& text, bool controller, int* slot);
constexpr int kButtonTexts = 4;

/* Changes to any binding since the last call (the prompts redrawn). */
unsigned Generation();

}  // namespace controls
