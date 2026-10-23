/*
 * A DualSense's adaptive triggers while racing (triggers.cpp): the
 * accelerator's and the brake's resistance from the car's own physics.
 */

#pragma once

#include "ps2host/run.h"

namespace triggers {

/* Once a pad read: the accelerator's and the brake's effects, from the
   player's car and how far each trigger is pulled (0-1); off while not
   racing, or with TRIGGER EFFECTS off. */
void Tick(bool racing, float throttle, float brake, ps2host::HostTriggerEffect* accel,
          ps2host::HostTriggerEffect* brake_effect);

}  // namespace triggers
