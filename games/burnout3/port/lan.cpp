/*
 * LAN play (on unless B3_LAN=0; B3_LAN_DEBUG=1 tells it step by step): the
 * game's own ONLINE mode, with the PC's network in place of the PS2's.
 *
 * The network itself is EA's DirtySock module (DRTYSCKF.IRX), run in the IOP
 * interpreter on the host's sockets (iop_servers.cpp, engine iop_inet.cpp).
 * What is here is what the game's EE side needs on top:
 *
 * - The network configuration. On a PS2 the ONLINE flow loads a "Your
 *   Network Configuration" file from the memory card (ezNetCnf) and brings
 *   the adaptor up with it (ezNetCtl). Its object has a mode for an interface
 *   that is already up (its byte +0x4D): the loading is skipped (0x0022DEF0)
 *   and the interface's address is read through Sony's libnet (0x0022DB78 ->
 *   0x0022DE50). The PC's interface is always up: that mode is set, and the
 *   few libnet calls it makes are answered here (libnet's IOP side, an
 *   MSIFRPC server, does not run).
 * - The lobby's redirector in the clear (PlainRedirector).
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "ps2host/host.h"
#include "ps2host/run.h"

bool LanEnabled();                 // iop_servers.cpp

/* B3_LAN_DEBUG=1: what the LAN does, step by step, in the log - the game's
   online flow, the lobby's messages, libnet's traffic. Off, only what a
   player needs to read is told. */
bool LanDebug() {
    static const bool on = [] {
        const char* value = std::getenv("B3_LAN_DEBUG");
        return value && *value && *value != '0';
    }();
    return on;
}
std::vector<std::pair<std::string, std::string>> LanNetworkList();     // lan_lobby.cpp: (address, adapter)
void LanPrepareLobby();            // lan_lobby.cpp: finds the LAN's lobby, or serves it

// The network configuration's step, each frame (0x0022E110: by its state at
// +0x48, the loading 0x0022DEF0, the bring-up 0x0022DB20).
void F_0022E110(lifted::Context& c);
// The memory card's check (0x0030A130 on the card object 0x005185A0): 0 the
// card is fine, 0xD it is not (missing, unformatted, no network file).
void F_0030A130(lifted::Context& c);
// The list of network configurations, each frame (its screen: the cursor in
// the low half of +0x78, from 0; +4 -1 until a line is taken, then the
// cursor plus one).
void F_003464B0(lifted::Context& c);
// DEBUG: the online flow's object, each frame.
void F_0026D2A0(lifted::Context& c);
void F_002714C0(lifted::Context& c);
void SixtyFpsMovieRate(lifted::Context& c);  // sixty_fps.cpp: 0x00130D50 at 60 FPS
void F_003530F0(lifted::Context& c);
void F_004020F8(lifted::Context& c);
void F_00402260(lifted::Context& c);
void F_004022E8(lifted::Context& c);
void F_0040F280(lifted::Context& c);
void F_00402C88(lifted::Context& c);
// Accessing the network, after the account (0x0022EB30 at its start, EA's
// NAT test and UPnP's search; 0x0022EB20 once they gave up).
void F_0022EB30(lifted::Context& c);
void F_0022EB20(lifted::Context& c);
// The network test's UPnP search (0x00233C50(test), its step at +0x598).
void F_00233C50(lifted::Context& c);
void F_00406E20(lifted::Context& c);
void F_004032F8(lifted::Context& c);
void F_00402FF8(lifted::Context& c);
// SELECT EA ACCOUNT's event handler (see AccountSelect).
void F_0034B9D0(lifted::Context& c);
// SELECT EA ACCOUNT's list of accounts, loaded from the card (0x00349840(list)).
void F_00349840(lifted::Context& c);
void F_00330360(lifted::Context& c);
void F_002366E0(lifted::Context& c);
void F_0033C390(lifted::Context& c);
void F_00315560(lifted::Context& c);
void F_00316E80(lifted::Context& c);
void F_0032D080(lifted::Context& c);
void F_00403C30(lifted::Context& c);
void F_00403DA0(lifted::Context& c);
void F_00403E28(lifted::Context& c);
void F_00403F00(lifted::Context& c);
void F_00403D38(lifted::Context& c);
void F_0031FE40(lifted::Context& c);
void F_0026EEC0(lifted::Context& c);
void F_00338C20(lifted::Context& c);
void F_0022D3B0(lifted::Context& c);
void F_0022FBB0(lifted::Context& c);
void F_00235D90(lifted::Context& c);
void F_0030F620(lifted::Context& c);
void F_002176D0(lifted::Context& c);
void F_00352210(lifted::Context& c);
void F_00400808(lifted::Context& c);
void F_00400F68(lifted::Context& c);
void F_004008E8(lifted::Context& c);
// The lobby's redirector is handed over in these two (see PlainRedirector).
void F_00346DF0(lifted::Context& c);
void F_003471E0(lifted::Context& c);

namespace {

// The wall clock in milliseconds, as lan_libnet.cpp's datagram dump: two
// instances' logs line up.
void Log(const std::string& text) {
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count() % 1000000;
    std::fprintf(stderr, "[lan] %06lld %s\n", ms, text.c_str());
}

// This PC's IPv4 on the LAN, network order: B3_LAN_IP if set (by hand, or
// the network chosen in the game's list), else the address a UDP socket
// "connected" toward the lobby (or outward) would send from (nothing is sent).
u32 LocalAddress() {
    u32 found = 0;
#ifdef _WIN32
    if (const char* text = std::getenv("B3_LAN_IP")) {
        in_addr a{};
        if (inet_pton(AF_INET, text, &a) == 1) return u32(a.s_addr);
    }
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return u32(0x0100007Fu);
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        sockaddr_in out{};
        out.sin_family = AF_INET;
        out.sin_port = htons(9);
        const char* host = std::getenv("PS2HOST_INET_HOST");
        if (!host || inet_pton(AF_INET, host, &out.sin_addr) != 1 || out.sin_addr.s_addr == htonl(0x7F000001u))
            inet_pton(AF_INET, "10.0.0.1", &out.sin_addr);
        sockaddr_in a{};
        int size = sizeof a;
        if (connect(s, reinterpret_cast<sockaddr*>(&out), sizeof out) == 0 &&
            getsockname(s, reinterpret_cast<sockaddr*>(&a), &size) == 0)
            found = a.sin_addr.s_addr;
        closesocket(s);
    }
#endif
    return found ? found : u32(0x0100007Fu);
}

std::string Dotted(u32 ip) {
    char text[16];
    std::snprintf(text, sizeof text, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
    return text;
}

void Return(lifted::Context& c, s32 value) { c.r[2] = s64(value); }

/* The combinations the screen lists (ezNetCnfGetCombinationList's, at
   0x01D7AF00; 0x0022E6B0 hands it out): the count +0, the default's number
   +4 (from 1), and from +0x30 entries of 0xC8 bytes - a status +0, a type +4
   (3: not listed), the name +0x48 (64 bytes), a second text +0x88. */
constexpr u32 kCombinations = 0x01D7AF00u, kCombination = 0xC8u;
// Lines in that list: AUTO and the PC's networks (the space after it, as
// far as is known, is the list's own - 10 on a PS2's memory card).
constexpr size_t kNetworkLines = 8;
std::vector<std::pair<std::string, std::string>> g_networks;   // the list's lines: (address, adapter)
bool g_address_by_hand = false;     // B3_LAN_IP set before the game started
bool g_lobby_ready = false;         // the lobby found or served since LAN was taken
bool g_list_stale = true;           // the list of networks to be made again (each time LAN is taken)

/* "Accessing network": before the lobby, the game tests the network
   (0x00230600(test), each frame, its state at +0x584: 1 sends EA's NAT probe
   to 81.6.198.229:2034, 2 searches a UPnP router, 3 on map a port on it; 7
   is given up after 60 s, 8 no router, 9 a port mapped). The search
   (0x00233C50, its step at +0x598) sends an M-SEARCH to 239.255.255.250:1900,
   waits 121 frames (~2 s) for an answer, and tries 4 times before its 9, no
   router - 8 s or more for nothing on a LAN. UpnpSearch answers 9 at once.
   The wait is timed in the log, from 0x0022EB30 to 0x0022EB20 (or the
   lobby's lookup, or 30 s). */
std::chrono::steady_clock::time_point g_accessing_since;
bool g_accessing = false;

void StopAccessing(const char* why) {
    if (!g_accessing) return;
    g_accessing = false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                          g_accessing_since).count();
    Log("accessing the network: " + std::to_string(ms) + " ms, " + why);
}

void WriteText(u32 at, const char* text, u32 room) {
    u32 i = 0;
    for (; text[i] && i + 1 < room; ++i) generated::Write8(at + i, u8(text[i]));
    for (; i < room; ++i) generated::Write8(at + i, 0);
}

std::string Upper(std::string text) {
    for (char& ch : text) ch = char(std::toupper(u8(ch)));
    return text;
}

/* 0x0022E110(object): the network configuration's step. The object's +0x4D
   set: the interface is taken as already up, nothing is loaded - and the
   list gets the PC's networks: AUTO (all of them), then each adapter's, by
   its address (two ZeroTier networks share an adapter name) with the
   adapter's name below it. */
void NetworkConfiguration(lifted::Context& c) {
    const u32 object = u32(c.r[4]);
    if (!generated::ReadU8(object + 0x4D)) {
        generated::Write8(object + 0x4D, 1);
        Log("network configuration: the interface taken as already up");
    }
    F_0022E110(c);
    if (g_accessing && std::chrono::steady_clock::now() - g_accessing_since > std::chrono::seconds(30))
        StopAccessing("given up after 30 s");
    if (generated::ReadU32(kCombinations) == 0 || g_list_stale) {
        g_list_stale = false;
        std::vector<std::pair<std::string, std::string>>& lines = g_networks;
        lines = {{"AUTO", "ALL NETWORKS"}};
        for (const auto& [address, adapter] : LanNetworkList())
            if (lines.size() < kNetworkLines) lines.emplace_back(address, Upper(adapter));
        for (u32 n = 0; n < lines.size(); ++n) {
            const u32 entry = kCombinations + 0x30 + n * kCombination;
            for (u32 i = 0; i < kCombination; i += 4) generated::Write32(entry + i, 0);
            generated::Write32(entry + 4, 1);
            WriteText(entry + 0x48, lines[n].first.c_str(), 0x40);
            WriteText(entry + 0x88, lines[n].second.c_str(), 0x40);
            Log("network list, line " + std::to_string(n + 1) + ": " + lines[n].first + " - " + lines[n].second);
        }
        generated::Write32(kCombinations + 4, 1);
        generated::Write32(kCombinations, u32(lines.size()));
    }
}

/* The network the LAN goes through: the list's line, as B3_LAN_IP (which
   the lobby, the libnet sockets and this PC's address all follow) - none
   for AUTO, all of them. A B3_LAN_IP set by hand stands. */
void UseNetwork(u32 line) {
    if (g_address_by_hand) {
        Log("network chosen: line " + std::to_string(line + 1) + ", but B3_LAN_IP is set by hand: it stands");
        return;
    }
#ifdef _WIN32
    if (line == 0 || line >= g_networks.size()) {
        _putenv_s("B3_LAN_IP", "");
        Log("network chosen: AUTO, all networks");
    } else {
        const std::string& address = g_networks[line].first;
        bool up = false;
        for (const auto& [now, adapter] : LanNetworkList()) up = up || now == address;
        if (up) {
            _putenv_s("B3_LAN_IP", address.c_str());
            Log("network chosen: " + address + " (" + g_networks[line].second + ")");
        } else {
            _putenv_s("B3_LAN_IP", "");
            Log("network chosen: " + address + " (" + g_networks[line].second +
                "), but it is not up any more (a VPN disconnected?): all networks are used instead");
        }
    }
#endif
}

/* 0x003464B0(screen): the list of network configurations. When a line is
   taken, the LAN goes through its network, and only then is the lobby found
   or served (before, it would be on a network not yet chosen). */
void ChooseNetwork(lifted::Context& c) {
    const u32 screen = u32(c.r[4]);
    F_003464B0(c);
    static s32 last = -1;
    const s32 line = s32(generated::ReadU32(screen + 4));
    if (line != last && line >= 0) {
        // The line taken is the cursor's (the low half of +0x78, from 0 for
        // the list's first line); +4 holds one more (5 for the fifth line,
        // shown "5-").
        const u32 cursor = generated::ReadU32(screen + 0x78) & 0xFFFF;
        if (LanDebug()) Log("network list: +4 " + std::to_string(line) + ", cursor " + std::to_string(cursor));
        UseNetwork(cursor);
        LanPrepareLobby();
        g_lobby_ready = true;
    }
    last = line;
}

/* 0x0030A130(card, 0, 0): the memory card's check. The NETWORK
   CONFIGURATION screen (0x00349E00, its state at +4) checks the card first
   (states 1 and 2, the calls returning to 0x00349F54 and 0x00349F90) and
   only then polls the network configuration (state 0, 0x0022F320: 2 is
   done). The list of configurations that follows (0x003464B0, its state at
   +0x88) checks it again each frame (returning to 0x00346520) and leaves if
   it is not fine. There is no "Your Network Configuration" file to read on
   a PC: for those screens the card is fine. Every other check (the game's
   saves) is the game's. */
void CardCheck(lifted::Context& c) {
    const u32 caller = u32(c.r[31]);
    if (caller == 0x00349F54u || caller == 0x00349F90u || caller == 0x00346520u) {
        static bool told = false;
        if (!told) {
            told = true;
            Log("NETWORK CONFIGURATION: no card needed");
        }
        return Return(c, 0);
    }
    F_0030A130(c);
}

// --- libnet on the EE (its client calls; each would be an MSIFRPC call to
// LIBNET.IRX through 0x003D3158).

/* 0x0023A740(mode): sceSifMInitRpc. Nothing to start. */
void MInitRpc(lifted::Context& c) { Return(c, 0); }

/* 0x003D4070(client, ...): libnet's initialisation, binding its server. */
void LibnetInit(lifted::Context& c) { Return(c, 0); }

/* 0x003D41B8(client, buffer): procedure 0x1E; its answer the buffer's first word. */
void LibnetRegister(lifted::Context& c) { Return(c, 0); }

/* 0x003D3BB8(client, buffer, ids, max): sceInetGetInterfaceList - one, id 1. */
void GetInterfaceList(lifted::Context& c) {
    const u32 ids = u32(c.r[6]), max = u32(c.r[7]);
    if (max == 0) return Return(c, 0);
    generated::Write32(ids, 1);
    Return(c, 1);
}

/* 0x003D3C40(client, buffer, id, code, data, length): sceInetInterfaceControl.
   Code 9: the address, a sceInetAddress (its IPv4 at +4). */
void InterfaceControl(lifted::Context& c) {
    const u32 code = u32(c.r[7]), data = u32(c.r[8]), length = u32(c.r[9]);
    for (u32 i = 0; i + 4 <= length; i += 4) generated::Write32(data + i, 0);
    if (code == 9 && length >= 8) {
        generated::Write32(data + 4, ntohl(LocalAddress()));     // libnet's addresses: the EE's order
    } else {
        static bool told[64] = {};
        if (code < 64 && !told[code]) {
            told[code] = true;
            Log("libnet sceInetInterfaceControl code " + std::to_string(code) + " (length " +
                std::to_string(length) + "): answered with zeros");
        }
    }
    Return(c, 0);
}

/* 0x003D34E8(client, buffer, text, length, address): sceInetAddress2String. */
void Address2String(lifted::Context& c) {
    const u32 text = u32(c.r[6]), length = u32(c.r[7]), address = u32(c.r[8]);
    const std::string dotted = Dotted(htonl(u32(generated::ReadU32(address + 4))));
    u32 i = 0;
    for (; i + 1 < length && i < dotted.size(); ++i) generated::Write8(text + i, u8(dotted[i]));
    if (length) generated::Write8(text + i, 0);
    Log("this PC's address for the game: " + dotted);
    Return(c, 0);
}

// --- DNAS, Sony's online authentication (its servers are gone).
//
// Its EE half is a relocatable ELF that DirtyElf loads (0x0041BE58) and
// links into four entry points (0x0041BE28..0x0041BE40); unloaded, each traps
// (0x0041C718, "DirtyElf: Error - attempt to call relocatable function when
// not loaded!"). The game's calls (0x0026C0A0 starts, 0x0026BEE0 polls):
//   0x0041BE28(params, mode)   start
//   0x0041BE30(wait)           1 while it runs, 0 when done
//   0x0041BE38(&result)        the result: its first word >= 0 is a pass
// then 0x0041BF90 unloads it. Here it passes at once.

/* 0x0041BE28, 0x0041BE40: started (nothing to run). */
void DnasStart(lifted::Context& c) {
    static bool told = false;
    if (!told) {
        told = true;
        Log("DNAS: passed (no authentication)");
    }
    Return(c, 0);
}

/* 0x0041BE30: done. */
void DnasRunning(lifted::Context& c) { Return(c, 0); }

/* 0x0041BE38(&result): a pass. */
void DnasResult(lifted::Context& c) {
    const u32 result = u32(c.r[4]);
    for (u32 i = 0; i < 0x20; i += 4) generated::Write32(result + i, 0);
    Return(c, 0);
}

/* 0x0041BF90: unloads the ELF - none was loaded. */
void DnasUnload(lifted::Context& c) { Return(c, 0); }

/* DEBUG (B3_LAN_DEBUG=1): the online flow's object each frame (0x0026D2A0): its
   states at +0x329E8, +0x329EC and flags +0x329E2/3, logged as they change. */
void OnlineProbe(lifted::Context& c) {
    const u32 o = u32(c.r[4]) + 0x30000u;
    static u32 last[4] = {~0u, ~0u, ~0u, ~0u};
    F_0026D2A0(c);
    const u32 now[4] = {u32(generated::ReadU32(o + 0x29E8)), u32(generated::ReadU32(o + 0x29EC)),
                        u32(generated::ReadU8(o + 0x29E2)), u32(generated::ReadU8(o + 0x29E3))};
    if (std::memcmp(now, last, sizeof now) != 0) {
        char text[160];
        std::snprintf(text, sizeof text, "probe online: state %u, sub %u, flags %u/%u, returned %d",
                      now[0], now[1], now[2], now[3], int(s32(c.r[2])));
        Log(text);
        std::memcpy(last, now, sizeof now);
    }
}

/* DEBUG (B3_LAN_DEBUG=1): the online flow's outer step (0x002714C0), its state
   at +0x32664, logged as it changes. */
void OuterProbe(lifted::Context& c) {
    const u32 o = u32(c.r[4]) + 0x30000u;
    static u32 last = ~0u;
    static s32 last_result = 0x7FFFFFFF;
    F_002714C0(c);
    const u32 now = generated::ReadU32(o + 0x2664);
    const s32 result = s32(c.r[2]);
    if (now != last || result != last_result) {
        Log("probe outer: state " + std::to_string(now) + ", returned " + std::to_string(result));
        last = now;
        last_result = result;
    }
}

/* DEBUG (B3_LAN_DEBUG=1): 0x00130D50(movie), its state +0x2F8 and result. */
void MovieProbe(lifted::Context& c) {
    const u32 movie = u32(c.r[4]), caller = u32(c.r[31]);
    const u32 before = generated::ReadU32(movie + 0x2F8);
    SixtyFpsMovieRate(c);   // sixty_fps.cpp's override of the same function, kept
    static u32 last = ~0u;
    const u32 key = before << 8 | (u32(c.r[2]) & 0xFF);
    if (key != last) {
        char text[128];
        std::snprintf(text, sizeof text, "probe movie %08X from %08X: state %u -> %u, returned %d", movie,
                      caller, before, generated::ReadU32(movie + 0x2F8), int(s32(c.r[2])));
        Log(text);
        last = key;
    }
}

/* DEBUG (B3_LAN_DEBUG=1): the authentication screen's update (0x003530F0): the
   frontend's mode 0x01E90424, its +8 and +4, 0x01E654C8. */
void ScreenProbe(lifted::Context& c) {
    const u32 screen = u32(c.r[4]);
    F_003530F0(c);
    const u32 now[4] = {u32(generated::ReadU32(0x01E90424u)), u32(generated::ReadU8(screen + 8)),
                        u32(generated::ReadU32(screen + 4)), u32(generated::ReadU32(0x01E654C8u))};
    static u32 last[4] = {~0u, ~0u, ~0u, ~0u};
    static u32 calls = 0;
    ++calls;
    if (std::memcmp(now, last, sizeof now) != 0) {
        char text[160];
        std::snprintf(text, sizeof text, "probe screen %08X (call %u): mode %u, +8 %u, +4 %u, 1E654C8 %d", screen,
                      calls, now[0], now[1], now[2], int(s32(now[3])));
        Log(text);
        std::memcpy(last, now, sizeof now);
    }
}

/* DEBUG (B3_LAN_DEBUG=1): DirtySock's network calls on the EE: NetConnStartup
   (0x004020F8), 'netw'/'star' (0x00402260), 'netw'/'stat' (0x004022E8). */
template <void (*Original)(lifted::Context&), int Which>
void NetProbe(lifted::Context& c) {
    static const char* const kNames[] = {"NetConnStartup", "netw/star", "netw/stat"};
    const u32 a0 = u32(c.r[4]), a1 = u32(c.r[5]), caller = u32(c.r[31]);
    Original(c);
    static u32 last_key[3] = {~0u, ~0u, ~0u};
    const u32 key = a0 ^ (u32(c.r[2]) << 1);
    if (key != last_key[Which]) {
        last_key[Which] = key;
        char text[200];
        std::snprintf(text, sizeof text, "probe %s(%08X, %08X) from %08X -> %d", kNames[Which], a0, a1, caller,
                      int(s32(c.r[2])));
        Log(text);
    }
}

std::string GuestString(u32 at, u32 limit = 64) {
    std::string text;
    for (u32 i = 0; at && i < limit; ++i) {
        const char ch = char(generated::ReadU8(at + i));
        if (!ch) break;
        text += ch >= 32 && ch < 127 ? ch : '.';
    }
    return text;
}

/* DEBUG (B3_LAN_DEBUG=1): 0x0040F280(text, key, ...) - a key's value in a
   message's body, or 0. */
void KeyProbe(lifted::Context& c) {
    const u32 text = u32(c.r[4]), key = u32(c.r[5]), caller = u32(c.r[31]);
    F_0040F280(c);
    Log("probe key " + GuestString(key, 16) + " from " + std::to_string(caller) + " in [" + GuestString(text, 48) +
        "] -> " + (u32(c.r[2]) ? "[" + GuestString(u32(c.r[2]), 24) + "]" : std::string("none")));
}

/* 0x00233C50(test): the UPnP search, a step a frame; 9: no router. On a LAN
   there is none to find: no router, at once. */
void UpnpSearch(lifted::Context& c) {
    const u32 test = u32(c.r[4]);
    generated::Write32(test + 0x598, 9);
    c.r[2] = 9;
    static bool told = false;
    if (!told || LanDebug()) Log("accessing the network: no UPnP search (a LAN has no router to map ports on)");
    told = true;
}

void AccessStart(lifted::Context& c) {
    F_0022EB30(c);
    if (!g_lobby_ready || g_accessing) return;
    g_accessing = true;
    g_accessing_since = std::chrono::steady_clock::now();
}

void AccessDone(lifted::Context& c) {
    F_0022EB20(c);
    StopAccessing("its tests over");
}

/* DEBUG (B3_LAN_DEBUG=1): 0x00402C88(ref, name, address, port). */
void LobbyLookup(lifted::Context& c) {
    StopAccessing("the lobby looked up");
    // The lobby's name about to be looked up: if no network was taken in
    // the list (it did not show), the lobby is found or served now.
    if (!g_lobby_ready && u32(c.r[5]) && generated::ReadU8(u32(c.r[5]))) {
        LanPrepareLobby();
        g_lobby_ready = true;
    }
    if (LanDebug()) {
        char text[160];
        std::snprintf(text, sizeof text, "probe ProtoAriesConnect(name [%s], address %08X, port %u)",
                      GuestString(u32(c.r[5]), 40).c_str(), u32(c.r[6]), u32(c.r[7]));
        Log(text);
    }
    F_00402C88(c);
}

std::string FourCc(u32 v) {
    std::string t;
    for (int i = 3; i >= 0; --i) {
        const char ch = char((v >> (8 * i)) & 0xFF);
        t += ch >= 32 && ch < 127 ? ch : '.';
    }
    return t;
}

/* DEBUG (B3_LAN_DEBUG=1): the lobby's update (0x00406E20, its object's state, a
   four-letter code, at +8; its timer at +0xC), as they change. */
void LobbyProbe(lifted::Context& c) {
    const u32 lobby = u32(c.r[4]);
    F_00406E20(c);
    static u32 last_login = ~0u, last_room = ~0u;
    constexpr u32 kOnline = 0x01E3CD44u;    // the ONLINE object: its login's step +0x10C, wanted room +0x260
    const u32 login = generated::ReadU32(kOnline + 0x10C), room = generated::ReadU32(kOnline + 0x260);
    if (login != last_login || room != last_room) {
        Log("probe online: login step " + std::to_string(login) + ", room " + std::to_string(s32(room)) +
            ", flags " + std::to_string(generated::ReadU8(kOnline + 0x110)) + "/" +
            std::to_string(generated::ReadU8(kOnline + 0x111)) + "/" + std::to_string(generated::ReadU8(kOnline + 0x112)));
        last_login = login;
        last_room = room;
    }
    static u32 last = ~0u;
    const u32 state = generated::ReadU32(lobby + 8);
    if (state != last) {
        Log("probe lobby: state '" + FourCc(state) + "', timer " + std::to_string(s32(generated::ReadU32(lobby + 0xC))) +
            ", last " + std::to_string(s32(generated::ReadU32(lobby + 0x10))));
        last = state;
    }
}

/* DEBUG (B3_LAN_DEBUG=1): 0x004032F8(ref, &cmd, &code, &body): a message waiting. */
void PeekProbe(lifted::Context& c) {
    const u32 cmd = u32(c.r[5]);
    F_004032F8(c);
    if (s32(c.r[2]) >= 0 && cmd)
        Log("probe peek: '" + FourCc(generated::ReadU32(cmd)) + "', " + std::to_string(s32(c.r[2])) + " bytes");
}

/* DEBUG (B3_LAN_DEBUG=1): EA's RPC to DRTYSCKF. Its state lives at 0x01EB6200:
   +0x4D94 the requests' ring, +0x4DA0 the replies' ring (32 bytes an entry,
   the sequence at +0x1C), +0x4DD4 the next free entry, +0x4DD8 the oldest
   waiting one, +0x4DF0/+0x4DF4 the polling alarms (0x00400808). */
constexpr u32 kRpc = 0x01EB6200u;
bool g_rpc_watch = false;           // set at the first ProtoAriesControl
u32 g_rpc_lines = 0;

std::string RpcState() {
    const u32 tail = generated::ReadU32(kRpc + 0x4DD8), head = generated::ReadU32(kRpc + 0x4DD4);
    const u32 req = generated::ReadU32(kRpc + 0x4D94) + tail * 32, rep = generated::ReadU32(kRpc + 0x4DA0) + tail * 32;
    char text[200];
    std::snprintf(text, sizeof text,
                  "tail %u head %u alarms %d/%d | req %08X seq %08X | rep %08X w1 %08X w5 %08X seq %08X", tail, head,
                  int(s32(generated::ReadU32(kRpc + 0x4DF0))), int(s32(generated::ReadU32(kRpc + 0x4DF4))), req,
                  generated::ReadU32(req + 0x1C), rep, generated::ReadU32(rep + 4), generated::ReadU32(rep + 0x14),
                  generated::ReadU32(rep + 0x1C));
    return text;
}

bool RpcLog() { return g_rpc_watch && g_rpc_lines++ < 400; }

void AlarmProbe(lifted::Context& c) {
    const s32 id = s32(c.r[4]);
    if (RpcLog()) Log("probe rpc alarm " + std::to_string(id) + " in: " + RpcState());
    F_00400808(c);
    if (RpcLog()) Log("probe rpc alarm " + std::to_string(id) + " out: " + RpcState());
}

void DispatchProbe(lifted::Context& c) {
    if (RpcLog()) Log("probe rpc dispatch in: " + RpcState());
    F_00400F68(c);
    if (RpcLog()) Log("probe rpc dispatch out (" + std::to_string(s32(c.r[2])) + "): " + RpcState());
}

void CallProbe(lifted::Context& c) {
    const u32 fn = u32(c.r[5]), flags = u32(c.r[10]);
    const u32 sync = generated::ReadU32(u32(c.r[29]));
    if (RpcLog()) {
        char text[80];
        std::snprintf(text, sizeof text, "probe rpc call fn %u flags %X sync %u: ", fn, flags, sync);
        Log(text + RpcState());
    }
    F_004008E8(c);
    if (RpcLog()) Log("probe rpc call -> " + std::to_string(s32(c.r[2])) + ": " + RpcState());
}

/* DEBUG (B3_LAN_DEBUG=1): DirtySock's debug printf (0x00400530(format, ...)), mute
   in the release build: printed, with its first arguments. */
void DebugPrintf(lifted::Context& c) {
    const std::string format = GuestString(u32(c.r[4]), 160);
    std::string out;
    int arg = 5;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%' || i + 1 >= format.size()) { out += format[i]; continue; }
        std::size_t j = i + 1;
        while (j < format.size() && std::strchr("-+ #0123456789.lh", format[j])) ++j;
        if (j >= format.size()) break;
        const char kind = format[j];
        const u64 value = arg <= 11 ? c.r[arg++] : 0;
        char text[64];
        switch (kind) {
            case 'd': case 'i': std::snprintf(text, sizeof text, "%d", int(s32(value))); out += text; break;
            case 'u': std::snprintf(text, sizeof text, "%u", u32(value)); out += text; break;
            case 'x': case 'X': case 'p': std::snprintf(text, sizeof text, "%x", u32(value)); out += text; break;
            case 'c': out += char(value); break;
            case 's': out += GuestString(u32(value), 80); break;
            case '%': out += '%'; --arg; break;
            default: out += '?'; break;
        }
        i = j;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '.')) out.pop_back();
    Log("dirtysock: " + out);
    c.r[2] = 0;
}

/* DEBUG (B3_LAN_DEBUG=1): who opens the online flow's error dialog. */
template <void (*Original)(lifted::Context&), u32 Address>
void CallerProbe(lifted::Context& c) {
    char text[160];
    std::snprintf(text, sizeof text, "probe %08X from %08X (a0 %08X a1 %08X a2 %08X a3 %08X)", Address,
                  u32(c.r[31]), u32(c.r[4]), u32(c.r[5]), u32(c.r[6]), u32(c.r[7]));
    Log(text);
    Original(c);
}

/* DEBUG (B3_LAN_DEBUG=1): the online flow's message handler (0x0030F620(obj,
   type, ?, code, reason)): the "game no longer exists" is code 0x28. */
void MessageProbe(lifted::Context& c) {
    if (u32(c.r[7]) == 0x28) {
        char text[200];
        std::snprintf(text, sizeof text, "probe message type %u code %u reason %u from %08X, stack %08X %08X %08X %08X",
                      u32(c.r[5]), u32(c.r[7]), u32(c.r[8]), u32(c.r[31]),
                      generated::ReadU32(u32(c.r[29]) + 0x0), generated::ReadU32(u32(c.r[29]) + 0x8),
                      generated::ReadU32(u32(c.r[29]) + 0x18), generated::ReadU32(u32(c.r[29]) + 0x28));
        Log(text);
    }
    F_0030F620(c);
}

void PostProbe(lifted::Context& c) {
    if (u32(c.r[5]) == 0x28) {
        char text[120];
        std::snprintf(text, sizeof text, "probe post code %u reason %u from %08X", u32(c.r[5]), u32(c.r[6]), u32(c.r[31]));
        Log(text);
    }
    F_002176D0(c);
}

/* DEBUG (B3_LAN_DEBUG=1): a function's arguments and result, its first calls. */
template <void (*Original)(lifted::Context&), u32 Address>
void ReturnProbe(lifted::Context& c) {
    static u32 told = 0;
    const u32 a0 = u32(c.r[4]), a1 = u32(c.r[5]), a2 = u32(c.r[6]), ra = u32(c.r[31]);
    Original(c);
    if (told++ < 60) {
        char text[160];
        std::snprintf(text, sizeof text, "probe %08X(%08X, %08X, %08X) from %08X -> %08X", Address, a0, a1, a2, ra,
                      u32(c.r[2]));
        Log(text);
    }
}

/* DEBUG (B3_LAN_DEBUG=1): a chat/game message reaching the ONLINE object
   (0x0033C390(obj, event, ...)): its flags (+0xC), the queue's count after. */
void IncomingProbe(lifted::Context& c) {
    static u32 told = 0;
    const u32 event = u32(c.r[5]);
    const u32 flags = generated::ReadU32(event + 0xC);
    F_0033C390(c);
    if (told++ < 30) {
        char text[120];
        std::snprintf(text, sizeof text, "probe incoming flags %08X, queue %d", flags,
                      int(s32(generated::ReadU32(u32(c.r[28]) - 0x58C8u))));
        Log(text);
    }
}

void GameMessagesProbe(lifted::Context& c) {
    static u32 told = 0;
    const u32 game = u32(c.r[4]);
    if (told++ < 20) Log("probe game messages: +4 = " + std::to_string(generated::ReadU8(game + 4)));
    F_0026EEC0(c);
}

void PopProbe(lifted::Context& c) {
    F_00338C20(c);
    static u32 told = 0;
    if (u32(c.r[2]) && told++ < 30) Log("probe pop: a message");
}

/* DEBUG (B3_LAN_DEBUG=1): the waiting room's menu (0x00315560(screen)): its items
   (+0x1288 + 8i: text, +4, +5; kind +0x12C0 + 4i), count +0x12DC, cursor +0x1284. */
void MenuProbe(lifted::Context& c) {
    const u32 m = u32(c.r[4]);
    static std::string last;
    const u32 count = generated::ReadU8(m + 0x12DC);
    char where[16];
    std::snprintf(where, sizeof where, "%08X", m);
    std::string now = std::string("probe menu ") + where + ": count " + std::to_string(count) + ", cursor " +
                      std::to_string(generated::ReadU16(m + 0x1284)) + ", 13AC " +
                      std::to_string(s32(s8(generated::ReadU8(m + 0x13AC)))) + " |";
    constexpr u32 kOnline = 0x01E3CD44u;
    now += " online " + std::to_string(generated::ReadU32(kOnline + 0x10C)) + "/" +
           std::to_string(generated::ReadU8(kOnline + 0x110)) + "/" + std::to_string(generated::ReadU8(kOnline + 0x111)) +
           "/" + std::to_string(generated::ReadU8(kOnline + 0x112)) + " |";
    for (u32 i = 0; i < count && i < 8; ++i)
        now += " [" + std::to_string(generated::ReadU8(m + 0x1288 + 8 * i + 4)) + "," +
               std::to_string(generated::ReadU8(m + 0x1288 + 8 * i + 5)) + ",k" +
               std::to_string(generated::ReadU32(m + 0x12C0 + 4 * i)) + "]";
    if (now != last) {
        Log(now);
        last = now;
    }
    F_00315560(c);
}

/* DEBUG (B3_LAN_DEBUG=1): 0x0031FE40(list, item, enabled): who greys a menu item. */
void EnableProbe(lifted::Context& c) {
    static std::string last;
    char text[120];
    std::snprintf(text, sizeof text, "probe enable item %u = %u from %08X", u32(c.r[5]), u32(c.r[6]), u32(c.r[31]));
    if (last != text) {
        Log(text);
        last = text;
    }
    F_0031FE40(c);
}

/* DEBUG (B3_LAN_DEBUG=1): the waiting room's state (+0x13AC) as it changes, and
   the messages it shows (0x0032D080(box, ?, text id, ...)). */
void RoomStateProbe(lifted::Context& c) {
    const u32 screen = u32(c.r[4]);
    F_00316E80(c);
    static s32 last = -999;
    const s32 now = s32(s8(generated::ReadU8(screen + 0x13AC)));
    if (now != last) {
        char text[80];
        std::snprintf(text, sizeof text, "probe room state %d -> %d", last, now);
        Log(text);
        last = now;
    }
}

void ShowProbe(lifted::Context& c) {
    char text[100];
    std::snprintf(text, sizeof text, "probe show text 0x%X (%u) from %08X", u32(c.r[6]), u32(c.r[7]), u32(c.r[31]));
    Log(text);
    F_0032D080(c);
}

/* DEBUG (B3_LAN_DEBUG=1): 0x00402FF8(ref, selector, ...): ProtoAries' controls. */
void ControlProbe(lifted::Context& c) {
    const u32 selector = u32(c.r[5]);
    Log("probe control '" + FourCc(selector) + "' ...");
    g_rpc_watch = true;
    F_00402FF8(c);
    Log("probe control '" + FourCc(selector) + "' -> " + std::to_string(s32(c.r[2])));
}

/* SELECT EA ACCOUNT (0x0034B9D0(screen, event)): the accounts come from the
   memory card's "EA Login" save. On a select (event 1) of an entry that is
   not a link to another screen (the u16 at 0x01E9B5BC is 1 for CREATE NEW /
   USE ANOTHER), it logs in with the entry's account: its name at 0x01E9B4D0
   (0x32 bytes), password at 0x01E9B502, a flag at 0x01E9B534, through
   0x0034CD50(screen, name, password, flag) -> the ONLINE object's auth.
   On the LAN there is no account to keep: with none loaded, the select
   logs in as the PC's player - B3_LAN_NAME, else the game's profile (its
   name, copied to 0x00516DF4 as the online flow opens: 0x0030E340, and
   MULTIPLAYER's LAN), else the Windows user's - which the LAN's lobby
   accepts as it is. */
constexpr u32 kAccountKind = 0x01E9B5BCu, kAccountName = 0x01E9B4D0u, kAccountPassword = 0x01E9B502u,
              kAccountFlag = 0x01E9B534u, kProfileName = 0x00516DF4u;

std::string PlayerName() {
    std::string raw;
    if (const char* name = std::getenv("B3_LAN_NAME")) raw = name;
    if (raw.empty()) {                  // the profile's name: UTF-16, up to 8 letters
        for (u32 i = 0; i < 8; ++i) {
            const u32 ch = generated::ReadU16(kProfileName + 2 * i);
            if (!ch) break;
            if (ch < 0x80) raw += char(ch);
        }
    }
#ifdef _WIN32
    if (raw.empty()) {
        char user[64] = {};
        DWORD size = sizeof user;
        if (GetUserNameA(user, &size)) raw = user;
    }
#endif
    std::string name;                   // the lobby's names: letters and digits, 4 to 16
    for (char ch : raw)
        if (std::isalnum(static_cast<unsigned char>(ch)) && name.size() < 16) name += ch;
    if (name.empty()) name = "PLAYER";
    while (name.size() < 4) name += '1';
    return name;
}

void AccountSelect(lifted::Context& c) {
    const u32 event = u32(c.r[5]);
    {   // PROBE (temporary): the screen's events, each the first time.
        static std::set<u32> seen;
        if (seen.insert(event).second) {
            char text[160];
            std::snprintf(text, sizeof text, "probe SELECT EA ACCOUNT event %u (a0 %08X a2 %08X a3 %08X) kind %u name[0] %u",
                          event, u32(c.r[4]), u32(c.r[6]), u32(c.r[7]), unsigned(generated::ReadU16(kAccountKind)),
                          unsigned(generated::ReadU8(kAccountName)));
            Log(text);
        }
    }
    // "No EA Account found" is greyed out: CREATE NEW EA ACCOUNT (event 1,
    // the u16 1) is the entry that, on the LAN, logs in as the PC's player.
    const auto LogIn = [&c] {
        const std::string name = PlayerName();
        WriteText(kAccountName, name.c_str(), 0x32);
        WriteText(kAccountPassword, "LANPLAY", 0x32);
        generated::Write8(kAccountFlag, 0);
        const u64 kind = generated::ReadU16(kAccountKind);
        generated::Write16(kAccountKind, 0);
        Log("SELECT EA ACCOUNT: logging in as " + name);
        F_0034B9D0(c);
        generated::Write16(kAccountKind, u16(kind));
    };
    if (event == 1 && generated::ReadU8(kAccountName) == 0) {
        LogIn();
        return;
    }
    F_0034B9D0(c);
}

/* A call into the game: arguments in a0.., the result (v0); the registers as they were. */
u32 Guest(lifted::Context& c, u32 target, std::initializer_list<u32> arguments) {
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    int r = 4;
    for (u32 a : arguments) c.r[r++] = s64(s32(a));
    lifted::Call(c, target, u32(saved[31]));
    const u32 result = u32(c.r[2]);
    std::memcpy(c.r, saved, sizeof saved);
    return result;
}

/* SELECT EA ACCOUNT coming in (0x0034A4E0, message 3, from the screen
   before it): its state (screen+0xB4) 1, the list of accounts loaded from
   the card (0x00349840(0x01E9B4CC)) to pick one from. On the LAN there is
   none to pick: instead, what selecting one does (0x0034BBC8) - state 2 and
   the login (0x0034CD50(0x01E9B740, name, password, flag)) as the PC's
   player; the screen goes by. The screen is in s0 (its handler's).
   B3_LAN_ACCOUNTS=1 shows it. */
constexpr u32 kAccountList = 0x01E9B4CCu, kAuth = 0x01E9B740u;

void AccountList(lifted::Context& c) {
    static const bool shown = [] {
        const char* value = std::getenv("B3_LAN_ACCOUNTS");
        return value && *value && *value != '0';
    }();
    const u32 screen = u32(c.r[16]);
    if (shown || u32(c.r[4]) != kAccountList || generated::ReadU32(screen + 0xB4) != 1) {
        F_00349840(c);
        return;
    }
    const std::string name = PlayerName();
    WriteText(kAccountName, name.c_str(), 0x32);
    WriteText(kAccountPassword, "LANPLAY", 0x32);
    generated::Write8(kAccountFlag, 0);
    generated::Write32(screen + 0xB4, 2);
    Guest(c, 0x0034CD50u, {kAuth, kAccountName, kAccountPassword, 0});
    Guest(c, 0x001272D0u, {screen + 0x10, kAccountName});     // as the select keeps them
    Guest(c, 0x001272D0u, {screen + 0x42, kAccountPassword});
    Log("SELECT EA ACCOUNT: logged in at once as " + name);
}

/* The lobby's redirector, "*ps2burnout05.ea.com" port 21800 (0x004C6E60),
   is handed to the lobby by 0x00346DF0 and 0x003471E0. The star asks for an
   encrypted connection (0x00406E6C skips it and turns 'cryp' on); without it
   the lobby talks in the clear, which the LAN's own lobby server does. */
constexpr u32 kRedirectorName = 0x004C6E60u;

template <void (*Original)(lifted::Context&)>
void PlainRedirector(lifted::Context& c) {
    if (generated::ReadU8(kRedirectorName) == '*') {
        u32 i = 0;
        for (u8 ch; (ch = u8(generated::ReadU8(kRedirectorName + i + 1))) != 0; ++i)
            generated::Write8(kRedirectorName + i, ch);
        generated::Write8(kRedirectorName + i, 0);
        Log("the lobby's redirector in the clear");
    }
    Original(c);
}

/* 'netw'/'stat'('plug') (0x004022E8): is the network cable in. DirtySock asks
   its IOP module, which now and then answers 0 for one read while the link is
   up (seen at the EA login, the game then going offline). A PC's network has
   no cable for the game to watch: the answer is always "in". */
void CableState(lifted::Context& c) {
    const u32 selector = u32(c.r[4]);
    NetProbe<F_004022E8, 2>(c);
    if (selector == 0x706C7567u && c.r[2] == 0) {      // 'plug'
        static bool told = false;
        if (!told) Log("the cable read as out: answered in");
        told = true;
        c.r[2] = 1;
    }
}

}  // namespace

/* This PC's address on the LAN, dotted (lan_lobby.cpp: how this game reaches
   the lobby it serves). */
std::string LanLocalAddressText() { return Dotted(LocalAddress()); }

/* B3_LAN_IP set before the game started (not by the game's list): the
   address the lobby names to everyone (lan_lobby.cpp, Discovery). */
bool LanAddressByHand() {
    return g_address_by_hand;
}

/* The network the online flow set up the last time, forgotten. The flow's
   first screen, SELECT EA ACCOUNT (0x0034A4E0, coming in), goes straight to
   the lobby when the flow's outer state (0x01E32AE0, 0x002714C0's +0x32664)
   is 23, done; else to NETWORK CONFIGURATION (0x00347440) - the list of
   networks, to choose one again (a player may have changed networks since).
   The outer state is put back as it was the first time LAN was taken. The
   online state (0x01E654C8, 9: DNAS and the ISP done) is kept: put back too,
   the flow went round AUTHENTICATING DNAS DATA and CONNECTING TO ISP for
   ever; kept, the outer flow's online step (0x0026D2A0) is done at once.
   B3_LAN_KEEP_NETWORK=1 keeps the network. */
void ForgetNetwork() {
    static const bool keep = [] {
        const char* value = std::getenv("B3_LAN_KEEP_NETWORK");
        return value && *value && *value != '0';
    }();
    constexpr u32 kOuterState = 0x01E32AE0u, kOnlineState = 0x01E654C8u, kOnlineFlag = 0x01E3C660u;
    char text[160];
    std::snprintf(text, sizeof text, "the online flow as LAN is taken: outer %d, online %d, flag %u%s",
                  int(s32(generated::ReadU32(kOuterState))), int(s32(generated::ReadU32(kOnlineState))),
                  unsigned(generated::ReadU8(kOnlineFlag)), keep ? " (kept)" : "");
    Log(text);
    // As they were the first time LAN was taken (before any network).
    static bool seen = false;
    static u32 outer = 0, online = 0, flag = 0;
    if (!seen) {
        seen = true;
        outer = generated::ReadU32(kOuterState);
        online = generated::ReadU32(kOnlineState);
        flag = generated::ReadU8(kOnlineFlag);
        return;
    }
    if (keep || generated::ReadU32(kOuterState) != 23) return;
    generated::Write32(kOuterState, outer);
    // TEST (B3_LAN_FORGET_ONLINE=1): the online state put back too - the list
    // of networks comes back, then the loop round DNAS and the ISP.
    const char* all = std::getenv("B3_LAN_FORGET_ONLINE");
    if (all && *all && *all != '0') {
        generated::Write32(kOnlineState, online);
        generated::Write8(kOnlineFlag, u8(flag));
        Log("the online flow's online state put back too (B3_LAN_FORGET_ONLINE)");
    }
    Log("the online flow's network forgotten: NETWORK CONFIGURATION again");
}

/* The game's own automatic login, primed: SELECT EA ACCOUNT's handler
   (0x0034A4E0), coming in (message 3), logs in at once with a remembered
   account when 0x01EA3150 is set - name 0x01EA3084, password 0x01EA30A4,
   through 0x0034CD50 - as after a race. Set as LAN is taken (overrides.cpp):
   the screen goes by, with the PC's player's name. */
void LanPresetLogin() {
    StopAccessing("LAN taken again");
    g_lobby_ready = false;
    g_list_stale = true;            // a VPN may have come or gone since
    const std::string name = PlayerName();
    WriteText(0x01EA3084u, name.c_str(), 0x20);
    WriteText(0x01EA30A4u, "LANPLAY", 0x20);
    generated::Write8(0x01EA3150u, 1);
    generated::Write8(kAccountName, 0);     // no account kept from the last time: SELECT EA ACCOUNT logs in anew
    Log("SELECT EA ACCOUNT will log in as " + name);
    ForgetNetwork();
}

/* The PC's player's name as the lobby knows it (lan_lobby.cpp: the room's). */
std::string LanPlayerName() { return PlayerName(); }

void RegisterLanLibnet();         // lan_libnet.cpp

void RegisterLan() {
    if (!LanEnabled()) return;
    g_address_by_hand = std::getenv("B3_LAN_IP") != nullptr;
    RegisterLanLibnet();
    ps2host::Override(0x0022E110u, NetworkConfiguration, "LAN: the network configuration, the interface already up");
    ps2host::Override(0x0030A130u, CardCheck, "LAN: the NETWORK CONFIGURATION screens need no card");
    ps2host::Override(0x003464B0u, ChooseNetwork, "LAN: the network chosen in the list of network configurations");
    ps2host::Override(0x0023A740u, MInitRpc, "LAN: sceSifMInitRpc (no MSIFRPC)");
    ps2host::Override(0x003D4070u, LibnetInit, "LAN: libnet's initialisation");
    ps2host::Override(0x003D41B8u, LibnetRegister, "LAN: libnet procedure 0x1E");
    ps2host::Override(0x003D3BB8u, GetInterfaceList, "LAN: libnet sceInetGetInterfaceList");
    ps2host::Override(0x003D3C40u, InterfaceControl, "LAN: libnet sceInetInterfaceControl");
    ps2host::Override(0x003D34E8u, Address2String, "LAN: libnet sceInetAddress2String");
    ps2host::Override(0x0041BE28u, DnasStart, "LAN: DNAS start (passes)");
    ps2host::Override(0x0041BE30u, DnasRunning, "LAN: DNAS state (done)");
    ps2host::Override(0x0041BE38u, DnasResult, "LAN: DNAS result (a pass)");
    ps2host::Override(0x0041BE40u, DnasStart, "LAN: DNAS entry 4");
    ps2host::Override(0x0041BF90u, DnasUnload, "LAN: DNAS unload (nothing loaded)");
    ps2host::Override(0x004022E8u, CableState, "LAN: the network cable is always in");
    ps2host::Override(0x00233C50u, UpnpSearch, "LAN: the network test's UPnP search, none");
    ps2host::Override(0x0022EB30u, AccessStart, "LAN: accessing the network, timed");
    ps2host::Override(0x0022EB20u, AccessDone, "LAN: accessing the network, over");
    ps2host::Override(0x00402C88u, LobbyLookup, "LAN: the lobby found, if not yet, before its name is looked up");
    ps2host::Override(0x00349840u, AccountList, "LAN: SELECT EA ACCOUNT logs in at once, no list");
    ps2host::Override(0x0034B9D0u, AccountSelect, "LAN: SELECT EA ACCOUNT logs in as the PC's player");
    ps2host::Override(0x00346DF0u, PlainRedirector<F_00346DF0>, "LAN: the lobby's redirector in the clear");
    ps2host::Override(0x003471E0u, PlainRedirector<F_003471E0>, "LAN: the lobby's redirector in the clear");
    if (!LanDebug()) return;
    Log("B3_LAN_DEBUG: the online flow, step by step");
    ps2host::Override(0x0026D2A0u, OnlineProbe, "LAN: DEBUG the online flow's states");
    ps2host::Override(0x002714C0u, OuterProbe, "LAN: DEBUG the online flow's outer state");
    ps2host::Override(0x00130D50u, MovieProbe, "LAN: DEBUG a movie's stop");
    ps2host::Override(0x003530F0u, ScreenProbe, "LAN: DEBUG the authentication screen");
    ps2host::Override(0x004020F8u, NetProbe<F_004020F8, 0>, "LAN: DEBUG NetConnStartup");
    ps2host::Override(0x00402260u, NetProbe<F_00402260, 1>, "LAN: DEBUG netw/star");
    ps2host::Override(0x0040F280u, KeyProbe, "LAN: DEBUG the lobby's key lookup");
    ps2host::Override(0x00406E20u, LobbyProbe, "LAN: DEBUG the lobby's update");
    ps2host::Override(0x004032F8u, PeekProbe, "LAN: DEBUG ProtoAries' peek");
    ps2host::Override(0x00402FF8u, ControlProbe, "LAN: DEBUG ProtoAriesControl");
    ps2host::Override(0x002176D0u, PostProbe, "LAN: DEBUG message posts");
    ps2host::Override(0x00403C30u, ReturnProbe<F_00403C30, 0x00403C30u>, "LAN: DEBUG mngl/crea");
    ps2host::Override(0x00403DA0u, ReturnProbe<F_00403DA0, 0x00403DA0u>, "LAN: DEBUG mngl/conn");
    ps2host::Override(0x00403E28u, ReturnProbe<F_00403E28, 0x00403E28u>, "LAN: DEBUG mngl/cmpl");
    ps2host::Override(0x00403F00u, ReturnProbe<F_00403F00, 0x00403F00u>, "LAN: DEBUG mngl/rept");
    ps2host::Override(0x00403D38u, ReturnProbe<F_00403D38, 0x00403D38u>, "LAN: DEBUG mngl/dest");
    ps2host::Override(0x0031FE40u, EnableProbe, "LAN: DEBUG menu items' enabling");
    ps2host::Override(0x00316E80u, RoomStateProbe, "LAN: DEBUG the waiting room's state");
    ps2host::Override(0x0032D080u, ShowProbe, "LAN: DEBUG messages shown");
    ps2host::Override(0x00315560u, MenuProbe, "LAN: DEBUG the waiting room's menu");
    ps2host::Override(0x0033C390u, IncomingProbe, "LAN: DEBUG incoming messages");
    ps2host::Override(0x0026EEC0u, GameMessagesProbe, "LAN: DEBUG the game's messages");
    ps2host::Override(0x00338C20u, PopProbe, "LAN: DEBUG message pop");
    ps2host::Override(0x002366E0u, ReturnProbe<F_002366E0, 0x002366E0u>, "LAN: DEBUG peer by id");
    ps2host::Override(0x0022D3B0u, ReturnProbe<F_0022D3B0, 0x0022D3B0u>, "LAN: DEBUG 0x0022D3B0");
    ps2host::Override(0x0022FBB0u, ReturnProbe<F_0022FBB0, 0x0022FBB0u>, "LAN: DEBUG 0x0022FBB0");
    ps2host::Override(0x00235D90u, ReturnProbe<F_00235D90, 0x00235D90u>, "LAN: DEBUG 0x00235D90");
    ps2host::Override(0x0030F620u, MessageProbe, "LAN: DEBUG the online flow's messages");
    ps2host::Override(0x00330360u, CallerProbe<F_00330360, 0x00330360u>, "LAN: DEBUG 0x00330360's callers");
    ps2host::Override(0x00352210u, CallerProbe<F_00352210, 0x00352210u>, "LAN: DEBUG 0x00352210's callers");
    ps2host::Override(0x00400530u, DebugPrintf, "LAN: DEBUG DirtySock's debug output");
    ps2host::Override(0x00400808u, AlarmProbe, "LAN: DEBUG the RPC's polling alarm");
    ps2host::Override(0x00400F68u, DispatchProbe, "LAN: DEBUG the RPC's replies");
    ps2host::Override(0x004008E8u, CallProbe, "LAN: DEBUG an RPC call");
}
