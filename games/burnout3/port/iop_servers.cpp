/*
 * Burnout 3's own IOP modules, as host RPC servers.
 *
 * These modules ship on the game's disc (cdrom0:\IOP\) rather than with the
 * SDK, so their servers belong to this port, not the engine (host.h). Each
 * protocol is read from the game's EE-side library, which builds the requests
 * and checks the answers.
 */

#include <cstdlib>

#include "ps2host/host.h"
#include "ps2host/iop_vm.h"
#include "ps2host/logitech.h"
#include "ps2host/run.h"

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
    // Logitech's wheels and headsets (LGDEVW.IRX, LGAUD.IRX): none attached,
    // the versions the game's libraries want (the engine's logitech.h).
    ps2host::logitech::RegisterNoDevices(0x010B1B00u);
    ps2host::logitech::RegisterNoHeadsets(0x010A0100u);
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
