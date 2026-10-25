/*
 * The game's functions as PC SETTINGS' MODS have them: for the port's own
 * overrides of a function a mod also changes (mods.cpp registers the mods'
 * first, so the port's wrapper stays in place and calls the mod's version).
 */
#pragma once

#include "ps2host/host.h"

// The mods' chooser for the function at `address` (mods_lifted.cpp), or
// `original` when no mod changes it.
ps2host::GuestFunction ModdedFunction(u32 address, ps2host::GuestFunction original);

// kOriginal as the mods have it: a wrapper calls this instead of F_X.
template <u32 kAddress, void (*kOriginal)(lifted::Context&)>
void Modded(lifted::Context& c) {
    static const ps2host::GuestFunction function = ModdedFunction(kAddress, kOriginal);
    function(c);
}
