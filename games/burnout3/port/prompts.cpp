/*
 * The button prompts as the player plays: the game's own (the DualShock's
 * buttons) while a controller is used, keys while the keyboard is, and the
 * mouse's buttons for choosing and going back while the mouse is. Whatever
 * was used last decides: a key or a click switches, as a controller's
 * button does back.
 *
 * The prompts are the frontend's button textures (the legend along the
 * bottom of its screens, and wherever else the game draws them): the port's
 * own art takes their place as a texture pack's would (texture_pack.h's
 * overrides; port/prompt_art.h, from port/tools/make_prompts.py), drawn in
 * the same places in them, so the game draws keys where it drew buttons.
 * The GPU renderer's alone - the software GS draws the game's.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "prompt_art.h"
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/texture_pack.h"

namespace {

enum class Prompts { kController, kKeyboard, kMouse };

Prompts g_prompts = Prompts::kController;
u64 g_input_seen = 0;           // LastInputDevice's frame, last time

template <std::size_t N>
void Override(const char* name, const unsigned char (&png)[N]) {
    if (!ps2host::texture_pack::SetOverride(name, png, N))
        std::fprintf(stderr, "[prompts] could not use the art for %s\n", name);
}

void Show(Prompts prompts) {
    using namespace prompt_art;
    namespace pack = ps2host::texture_pack;
    if (prompts == Prompts::kController) {
        pack::ClearOverrides();
    } else {
        if (prompts == Prompts::kMouse) Override(kAtlasMouseName, kAtlasMouse);
        else Override(kAtlasKeyboardName, kAtlasKeyboard);
        Override(kArrowKeysName, kArrowKeys);
        Override(kKeysLName, kKeysL);
        Override(kKeysRName, kKeysR);
    }
    std::fprintf(stderr, "[prompts] %s\n", prompts == Prompts::kController ? "the controller's"
                                           : prompts == Prompts::kKeyboard ? "the keyboard's"
                                                                           : "the mouse's");
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
            if (want == "keyboard" || want == "mouse") {
                g_prompts = want == "keyboard" ? Prompts::kKeyboard : Prompts::kMouse;
                Show(g_prompts);
            }
        }
    }
    u64 frame = 0;
    const ps2host::InputDevice device = ps2host::LastInputDevice(&frame);
    Prompts now = g_prompts;
    if (mouse_used) now = Prompts::kMouse;
    else if (frame != g_input_seen)
        now = device == ps2host::InputDevice::kKeyboard ? Prompts::kKeyboard : Prompts::kController;
    g_input_seen = frame;
    if (now != g_prompts) {
        g_prompts = now;
        Show(now);
    }
}
