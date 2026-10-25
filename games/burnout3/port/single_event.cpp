/*
 * SINGLE EVENT's ELIMINATOR: RACE, TIME ATTACK, ROAD RAGE, ELIMINATOR,
 * CRASH - the World Tour's Eliminator (the last car of each lap out) as a
 * single event, on any race track. The idea is bryan's (his "Eliminator -
 * Single Event" PCSX2 patch showed that the game's own rules do it); this is
 * the port's own way in, with the game's own strings and objects.
 *
 * What the game has:
 *   - SINGLE EVENT's list: a selector at 0x01E94E4C, made by 0x003C9210 -
 *     +0x4 its rows (4), +0x8 the chosen one, +0xC the title's string, +0x10
 *     on each row's string (RACE 0x15F, TIME ATTACK 0x161, ROAD RAGE 0x160,
 *     CRASH 0x165; a fifth slot, +0x20, unused), +0x38 on a byte each (a row
 *     greyed). The chosen row is the mode (0x01E900B8: 0-3), its index;
 *     0x003C9340 takes it as Cross goes down.
 *   - the event's rules: an object for each kind, made at start-up - the
 *     race's at 0x0051B258, the Eliminator's (kind 1, the World Tour's) at
 *     0x0051B2F0. The event's setups (0x0030C2C0, 0x0030C560) put one in the
 *     race's holder (+0x1B4); the World Tour's Eliminator with +0xA4 5.
 *   - 0x01E900B4: the game's kind of play - 1 SINGLE EVENT, 0 WORLD TOUR.
 *
 * Here: the list five rows, ELIMINATOR (the game's string 0x111) before
 * CRASH; ELIMINATOR taken as RACE (its tracks, its options), and the race's
 * rules then the Eliminator's - only in SINGLE EVENT, never the World Tour's
 * races after it.
 */

#include "ps2host/host.h"
#include "ps2host/run.h"

void F_003C98D0(lifted::Context& c);
void F_003C9340(lifted::Context& c);
void F_0030C2C0(lifted::Context& c);
void F_0030C560(lifted::Context& c);

namespace {

constexpr u32 kList = 0x01E94E4Cu;              // SINGLE EVENT's selector
constexpr u32 kRows = kList + 0x4u, kChosen = kList + 0x8u, kRowStrings = kList + 0x10u, kGreyed = kList + 0x38u;
constexpr u32 kMode = 0x01E900B8u;              // the chosen mode: 0 RACE ... 3 CRASH
constexpr u32 kPlay = 0x01E900B4u;              // 1: SINGLE EVENT
constexpr u32 kStringEliminator = 0x111u, kStringCrash = 0x165u;
constexpr u32 kRaceRules = 0x0051B258u, kEliminatorRules = 0x0051B2F0u;
constexpr u32 kEliminatorRow = 3, kCrashRow = 4;

bool g_eliminator = false;      // ELIMINATOR was the row chosen last

/* 0x003C98D0(selector): the list shown - from SINGLE EVENT's making
   (0x003C9210, returning to 0x003C932C), five rows first. */
void ShowList(lifted::Context& c) {
    if (u32(c.r[31]) == 0x003C932Cu && u32(c.r[4]) == kList) {
        generated::Write32(kRowStrings + 4u * kEliminatorRow, kStringEliminator);
        generated::Write32(kRowStrings + 4u * kCrashRow, kStringCrash);
        generated::Write8(kGreyed + kCrashRow, u8(generated::ReadU8(kGreyed + kEliminatorRow)));
        generated::Write8(kGreyed + kEliminatorRow, 0);
        generated::Write32(kRows, 5);
        // The row the mode was (the game's: its index), CRASH one down.
        const u32 mode = u32(generated::ReadU32(kMode));
        u32 row = generated::ReadU32(kChosen);
        if (mode == 3) row = kCrashRow;
        else if (mode == 0 && g_eliminator) row = kEliminatorRow;
        generated::Write32(kChosen, row);
    }
    F_003C98D0(c);
}

/* 0x003C9340(selector, ...): Cross on a row - the game's for the row it
   knows: ELIMINATOR as RACE, CRASH as its own fourth. */
void ChooseRow(lifted::Context& c) {
    // (SINGLE EVENT's own: it reads that selector's row, whatever a0 is)
    const bool cross = u32(c.r[5]) == 5u;      // (a1 5: Cross; 3 the way back)
    const u32 row = generated::ReadU32(kChosen);
    const u32 as = row == kEliminatorRow ? 0u : row == kCrashRow ? 3u : row;
    generated::Write32(kChosen, as);
    F_003C9340(c);
    if (u32(generated::ReadU32(kChosen)) == as) generated::Write32(kChosen, row);
    if (cross) g_eliminator = row == kEliminatorRow;
}

/* 0x0030C2C0, 0x0030C560: an event's setup, its rules in the race's holder
   (+0x1B4) - the Eliminator's for a SINGLE EVENT race chosen as ELIMINATOR,
   as the World Tour's Eliminator has them (+0xA4 5). */
template <void (*Setup)(lifted::Context&)>
void EventSetup(lifted::Context& c) {
    Setup(c);
    if (!g_eliminator || generated::ReadU32(kPlay) != 1u) return;
    // (the race's holder: one player's, 0x0051AD00 - the one 0x0051BA88 names)
    for (const u32 holder : {u32(generated::ReadU32(0x0051BA88u)), 0x0051AD00u}) {
        if (!holder || u32(generated::ReadU32(holder + 0x1B4u)) != kRaceRules) continue;
        generated::Write32(holder + 0x1B4u, kEliminatorRules);
        generated::Write32(holder + 0xA4u, 5);
        return;
    }
}

}  // namespace

void RegisterSingleEvent() {
    ps2host::Override(0x003C98D0u, ShowList, "a selector shown: SINGLE EVENT's five rows, ELIMINATOR");
    ps2host::Override(0x003C9340u, ChooseRow, "SINGLE EVENT's Cross: ELIMINATOR as RACE, CRASH fifth");
    ps2host::Override(0x0030C2C0u, EventSetup<F_0030C2C0>, "an event's setup: SINGLE EVENT's ELIMINATOR rules");
    ps2host::Override(0x0030C560u, EventSetup<F_0030C560>, "an event's setup: SINGLE EVENT's ELIMINATOR rules");
}
