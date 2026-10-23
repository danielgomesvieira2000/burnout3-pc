/*
 * DRIVER DETAILS > EXTRAS without PLAY NFS:UNDERGROUND 2 DEMO (issue 19).
 *
 * The disc's demo is another program (NFSUNDER/NFSUNDER.ELF): chosen and
 * confirmed, the game sets 0x0051BAD7 (0x00469AB0, the confirmation's
 * state answering YES), its main loop ends (0x0012EB30) and 0x001D3D70
 * shuts down to LoadExecPS2 it - a program the port has not been made
 * from: a freeze.
 *
 * The page (found with --watch and --watch-write in a scripted run):
 *   - its menu at 0x01E95BD8: the row chosen at +4 (an int), then from +0xC
 *     what the list is made from - the cursor (u16), the rows' count (u16,
 *     9) and a row each from +0x10, 8 bytes: its text, whether it is locked
 *     (+4), a byte (+5). The demo's is row 0 (string 0xBF9).
 *   - 0x00456E60(menu) makes the rows and hands them to the list
 *     (0x00321470(list 0x01E89544, the cursor and count, the rows), from
 *     0x004570A0), which keeps its own copy of each row's text - the row
 *     under the cursor it reads from the menu as it is.
 *   - 0x00456900, the page's state: a choice (message 5 with 0 or 2) goes
 *     by the row's number (+4) - row 0 the demo's confirmation (state
 *     0x94413FEB5B627369), the rest the videos and the credits.
 *
 * So the list is made of rows 1-8 alone (the cursor one less), and for a
 * choice the page sees the rows as the game made them, the row one more:
 * each row its own video, and the demo never chosen.
 */

#include "ps2host/host.h"
#include "ps2host/run.h"

void F_00456900(lifted::Context& c);

namespace {

constexpr u32 kMenu = 0x01E95BD8u;
constexpr u32 kChosen = kMenu + 0x4u, kCursor = kMenu + 0xCu, kCount = kMenu + 0xEu, kRows = kMenu + 0x10u;
constexpr u32 kRowBytes = 8u;

u32 g_demo_row[2] = {};         // the demo's row as the game made it
bool g_without = false;         // the menu's rows are without it

void ShiftUp(u32 count) {
    for (u32 i = 0; i + 1 < count; ++i)
        for (u32 b = 0; b < kRowBytes; b += 4)
            generated::Write32(kRows + i * kRowBytes + b, generated::ReadU32(kRows + (i + 1) * kRowBytes + b));
}

void ShiftDown(u32 count) {
    for (u32 i = count; i-- > 1;)
        for (u32 b = 0; b < kRowBytes; b += 4)
            generated::Write32(kRows + i * kRowBytes + b, generated::ReadU32(kRows + (i - 1) * kRowBytes + b));
}

/* The menu's rows without the demo's (its row saved), the cursor and the
   row chosen one less. */
void Remove() {
    const u32 count = u32(generated::ReadU16(kCount));
    if (count < 2 || generated::ReadU32(kRows) == 0) return;
    g_demo_row[0] = generated::ReadU32(kRows);
    g_demo_row[1] = generated::ReadU32(kRows + 4u);
    ShiftUp(count);
    generated::Write16(kCount, count - 1);
    const u32 cursor = u32(generated::ReadU16(kCursor));
    generated::Write16(kCursor, cursor ? cursor - 1 : 0);
    const s32 chosen = s32(generated::ReadU32(kChosen));
    generated::Write32(kChosen, u32(chosen > 0 ? chosen - 1 : 0));
    g_without = true;
}

/* ...and back as the game made them, the row chosen one more. */
void PutBack() {
    const u32 count = u32(generated::ReadU16(kCount)) + 1;
    ShiftDown(count);
    generated::Write32(kRows, g_demo_row[0]);
    generated::Write32(kRows + 4u, g_demo_row[1]);
    generated::Write16(kCount, count);
    generated::Write32(kChosen, generated::ReadU32(kChosen) + 1);
    g_without = false;
}

}  // namespace

/* 0x00456900(state, message, ..., what), the page's own (dev_menu.cpp's
   Extras, which has it, calls this for the game's EXTRAS): a choice made
   on the page sees the rows as the game made them. */
void ExtrasPage(lifted::Context& c) {
    const u32 message = u32(c.r[5]), what = u32(c.r[7]);
    const bool choice = message == 5u && (what == 0u || what == 2u) && g_without;
    if (choice) PutBack();
    F_00456900(c);
    if (choice && !g_without) Remove();
}


/* 0x00321470(list, cursor and count, rows), before it runs (overrides.cpp's
   MenuList, which has it): the EXTRAS page's list (from 0x00456E60) made
   without the demo's row. */
void ExtrasListMade(lifted::Context& c) {
    if (u32(c.r[31]) == 0x004570A0u && u32(c.r[5]) == kCursor) Remove();
}

