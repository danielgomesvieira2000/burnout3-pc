/*
 * Burnout 3's own IOP modules, as host RPC servers.
 *
 * These modules ship on the game's disc (cdrom0:\IOP\) rather than with the
 * SDK, so their servers belong to this port, not the engine (host.h). Each
 * protocol is read from the game's EE-side library, which builds the requests
 * and checks the answers.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/iop_vm.h"
#include "ps2host/run.h"

namespace {

/* LGDEVW.IRX, Logitech's USB device layer (server 0x046D046D, Logitech's USB
   vendor ID twice), which the wheel support uses. Its EE library, lgDevInit at
   0x004438E0, calls procedure 12 with a 0x240-byte buffer and reads the module's
   version from the answer's word at +4: 0x010B1B00 exactly, or it warns for any
   other 0x010Bxxxx and halts for anything else ("wrong version of lgDev.irx").
   No Logitech device is attached here.
     procedure 1   lgDevEnumerate(index, description*): the answer's first word
                   is the result, an error when bit 31 is set; the caller
                   (0x00442720) asks for index after index until one fails, so
                   with no device every index fails.
   Other procedures are noted and answered with zeros. */
std::vector<u8> LgDev(u32 procedure, const std::vector<u8>& data) {
    constexpr u32 kVersion = 0x010B1B00u;
    constexpr u32 kNoDevice = 0x80000001u;      // the library tests bit 31 only
    std::vector<u8> answer(data.size() < 8 ? 8 : data.size(), 0);
    if (procedure == 12) {
        std::memcpy(&answer[4], &kVersion, 4);
        return answer;
    }
    if (procedure == 1) {
        std::memcpy(&answer[0], &kNoDevice, 4);
        return answer;
    }
    char text[96];
    std::snprintf(text, sizeof text, "IOP: lgDev procedure %u is not implemented (zeros)",
                  unsigned(procedure));
    ps2host::Note("lgdev " + std::to_string(procedure), text);
    return answer;
}

/* LGAUD.IRX, Logitech's USB audio (server 0x50494C42, "PILB"): the headset
   the online mode's voice chat uses. Its EE library, lgAudInit at 0x002E0838
   (called when the online mode starts), calls procedure 0x10 with 0x20 bytes
   and reads the module's version from the answer's word at +8: 0x010A0100
   exactly, or it warns for any other 0x010Axxxx and halts for anything else
   ("wrong version of lgaud.irx"). No headset is attached here.
     procedure 1   lgAudEnumerate (0x002E1960): the answer's first word is the
                   result, an error when bit 31 is set (as lgDev's).
   Other procedures are noted and answered with zeros. */
std::vector<u8> LgAud(u32 procedure, const std::vector<u8>& data) {
    constexpr u32 kVersion = 0x010A0100u;
    constexpr u32 kNoDevice = 0x80000001u;
    std::vector<u8> answer(data.size() < 0x30 ? 0x30 : data.size(), 0);
    if (procedure == 0x10) {
        std::memcpy(&answer[8], &kVersion, 4);
        return answer;
    }
    if (procedure == 1) {
        std::memcpy(&answer[0], &kNoDevice, 4);
        return answer;
    }
    char text[96];
    std::snprintf(text, sizeof text, "IOP: lgAud procedure %u is not implemented (zeros)",
                  unsigned(procedure));
    ps2host::Note("lgaud " + std::to_string(procedure), text);
    return answer;
}

}  // namespace

bool LanEnabled();

void RegisterGameIopServers() {
    // RenderWare Audio, the game's sound driver: its protocol is its own (EE
    // side at 0x002AEFC0), so the module itself runs (iop_vm.h).
    ps2host::iop::RunModule("RWA.IRX");
    // EA's file system on the disc (RPC server "GTS", 0x00475453): how the
    // game loads its data. It reads the disc through cdvdman.
    ps2host::iop::RunModule("GTFSCDVD.IRX");
    // B3ROUTE: an RPC server of the game's own (0x00150276).
    ps2host::iop::RunModule("B3ROUTE.IRX");
    ps2host::RegisterRpcServer(0x046D046Du, LgDev);
    ps2host::RegisterRpcServer(0x50494C42u, LgAud);
    // EA's DirtySock module: the game's network (its sockets, SSL, NAT) and
    // EA's own RPC over SIF command 1. It runs, on the host's inet
    // (engine iop_inet.cpp). With the LAN on (LanEnabled).
    if (LanEnabled()) ps2host::iop::RunModule("DRTYSCKF.IRX");
}

/* The LAN play (MULTIPLAYER's LAN row and all it needs): on, unless B3_LAN=0
   - the upstream's recorded runs were made without it. */
bool LanEnabled() {
    static const bool on = [] {
        const char* value = std::getenv("B3_LAN");
        return !(value && *value == '0');
    }();
    return on;
}
