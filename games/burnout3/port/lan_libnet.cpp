/*
 * LAN play: Sony's libnet, answered on the EE with the PC's sockets.
 *
 * The race itself goes from player to player (UDP, port 3658), through the
 * game's own BSD-like socket layer (0x00237xxx) on top of Sony's libnet.
 * libnet's client (0x003D3xxx) packs each call into a buffer and sends it to
 * its IOP server through MSIFRPC (0x003D3158 -> sceSifMCallRpc), which the
 * port does not run. Its calls are answered here, each by its procedure
 * number (the one 0x003D3158 is given):
 *
 *   1 Create(param)            2 Open(id, ms)          3 Close(id, ms)
 *   4 Recv(id, data, length, &flags, ms)               5 Send(id, data, length, &flags, ms)
 *  13 RecvFrom(id, data, length, &flags, &address, &port, ms)
 *  14 SendTo(id, data, length, &flags, &address, port, ms)
 *  15 Abort(id, flags)        19 Control(id, code, buffer, length)
 *  20 Poll(entries, count, ms)
 *
 * Each wrapper is called as (client, buffer, then those arguments), in a2,
 * a3, t0..t3, then the stack. The structures are inet's: a sceInetParam of
 * 0x40 bytes (type +0: 0 datagram, 1 active, 2 passive, 3 raw; local port +4,
 * -1 any; remote address +8, a sceInetAddress whose IPv4 is at +4; remote port
 * +0x18). Addresses and ports are plain numbers in the EE's order (127.0.0.1
 * is 0x7F000001): the game's first datagram went to 250.255.255.239 when its
 * address was taken for network order - UPnP's 239.255.255.250.
 *
 * Nothing here ever waits: a call with nothing to give returns at once (0
 * bytes, or the time-out -502 when it was asked to wait), and the game's
 * layer comes back - the EE must not stop for the network.
 *
 * B3_LAN_IP=<address>: this instance's address, for its sockets and what it
 * tells the others (two instances on one PC: 127.0.0.1 and 127.0.0.2).
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "ps2host/host.h"
#include "ps2host/run.h"

bool LanEnabled();                 // iop_servers.cpp

bool LanDebug();                   // lan.cpp: B3_LAN_DEBUG=1

namespace {

constexpr s32 kDgram = 0, kActive = 1, kPassive = 2, kRaw = 3;
constexpr s32 kError = -1, kTimeout = -502, kNoConnection = -503;

void Log(const std::string& text) { std::fprintf(stderr, "[libnet] %s\n", text.c_str()); }

std::string Text(u32 ip_network, u32 port) {
    const u8* b = reinterpret_cast<const u8*>(&ip_network);
    char text[32];
    std::snprintf(text, sizeof text, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], port);
    return text;
}

// Every 2 seconds, a summary of the traffic: datagrams and bytes each way,
// reads that found nothing, polls, errors.
struct Stats { u32 sent = 0, sent_bytes = 0, got = 0, got_bytes = 0, empty = 0, polls = 0, errors = 0; };
Stats& Counts() {
    static Stats stats;
    return stats;
}

void Summary() {
    if (!LanDebug()) return;
    static DWORD last = GetTickCount();
    const DWORD now = GetTickCount();
    if (now - last < 2000) return;
    last = now;
    Stats& n = Counts();
    char text[200];
    std::snprintf(text, sizeof text, "traffic: sent %u (%u bytes), received %u (%u bytes), empty reads %u, polls %u, errors %u",
                  n.sent, n.sent_bytes, n.got, n.got_bytes, n.empty, n.polls, n.errors);
    std::fprintf(stderr, "[libnet] %s\n", text);
    n = Stats{};
}

// B3_LAN_DUMP=1: each datagram (up to 50,000) with its first bytes and the
// wall clock in milliseconds - the same for two instances on one PC, so
// their logs line up.
void Dump(const char* way, u32 ip, u32 port, const char* data, int size) {
    static const bool on = std::getenv("B3_LAN_DUMP") != nullptr;
    static u32 told = 0;
    if (!on || told++ > 50000) return;
    std::string hex;
    char b[4];
    for (int i = 0; i < size && i < 64; ++i) {
        std::snprintf(b, sizeof b, "%02X ", unsigned(static_cast<unsigned char>(data[i])));
        hex += b;
    }
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count() % 1000000;
    std::fprintf(stderr, "[libnet] %06lld %s %s %d: %s\n", ms, way, Text(ip, port).c_str(), size, hex.c_str());
    // Who sends or reads: each new guest call stack, once.
    static std::set<std::string> stacks;
    u32 calls[16];
    const int n = ps2host::GuestCallsNow(calls, 16);
    std::string stack;
    for (int i = 0; i < n; ++i) {
        char f[12];
        std::snprintf(f, sizeof f, " %08X", calls[i]);
        stack += f;
    }
    if (stacks.insert(std::string(way) + stack).second) std::fprintf(stderr, "[libnet] %s stack:%s\n", way, stack.c_str());
}

// With B3_LAN_DEBUG, the first calls of each kind are told; then every 500th.
bool Tell(int proc) {
    if (!LanDebug()) return false;
    static std::map<int, u32> seen;
    const u32 n = ++seen[proc];
    return n <= 12 || n % 500 == 0;
}

struct Socket {
    s32 type = kDgram;
    s32 local_port = -1;
    u32 remote_ip = 0;              // network order
    u32 remote_port = 0;
    SOCKET s = INVALID_SOCKET;
    SOCKET listener = INVALID_SOCKET;   // passive: waiting for its peer
    bool connected = false;
};

std::map<s32, Socket>& Sockets() {
    static std::map<s32, Socket> sockets;
    return sockets;
}

bool Started() {
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

// This instance's address (network order): B3_LAN_IP, or any.
u32 BindAddress() {
    in_addr a{};
    const char* text = std::getenv("B3_LAN_IP");
    return text && inet_pton(AF_INET, text, &a) == 1 ? u32(a.s_addr) : u32(INADDR_ANY);
}

void NonBlocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

bool WouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
}

sockaddr_in Address(u32 ip_network, u32 port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = ip_network;
    a.sin_port = htons(u16(port));
    return a;
}

u32 Arg(lifted::Context& c, int i) {        // the i-th argument after (client, buffer)
    if (i < 6) return u32(c.r[6 + i]);      // a2, a3, t0, t1, t2, t3
    return generated::ReadU32(u32(c.r[29]) + u32(i - 6) * 4);
}

void Return(lifted::Context& c, s32 value) { c.r[2] = s64(value); }

u32 HostIp(u32 guest) { return htonl(guest); }        // the EE's number -> network order
u32 GuestIp(u32 network) { return ntohl(network); }

Socket* Find(s32 id) {
    auto it = Sockets().find(id);
    return it == Sockets().end() ? nullptr : &it->second;
}

// 1: Create(param) -> id
void Create(lifted::Context& c) {
    const u32 p = Arg(c, 0);
    static s32 next = 1;
    Socket k;
    k.type = s32(generated::ReadU32(p));
    k.local_port = s32(generated::ReadU32(p + 4));
    k.remote_ip = HostIp(u32(generated::ReadU32(p + 0xC)));
    k.remote_port = u32(generated::ReadU32(p + 0x18));
    const s32 id = next++;
    Sockets()[id] = k;
    if (Tell(1))
        Log("create " + std::to_string(id) + ": type " + std::to_string(k.type) + ", local port " +
            std::to_string(k.local_port) + ", remote " + Text(k.remote_ip, k.remote_port));
    Return(c, id);
}

// 2: Open(id, ms)
void Open(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    Socket* k = Find(id);
    if (!k || !Started()) return Return(c, kNoConnection);
    if (k->s != INVALID_SOCKET || k->listener != INVALID_SOCKET) return Return(c, 0);
    const u32 port = k->local_port > 0 ? u32(k->local_port) : 0;
    if (k->type == kDgram || k->type == kRaw) {
        // Raw (pings) is answered as datagrams nobody reads: no privilege needed.
        k->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int on = 1;
        setsockopt(k->s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
        const sockaddr_in a = Address(BindAddress(), k->type == kRaw ? 0 : port);
        if (bind(k->s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
            Log("open " + std::to_string(id) + ": udp bind to " + Text(BindAddress(), port) + " failed (" +
                std::to_string(WSAGetLastError()) + ")");
            closesocket(k->s);
            k->s = INVALID_SOCKET;
            return Return(c, kError);
        }
        NonBlocking(k->s);
        sockaddr_in bound{};
        int size = sizeof bound;
        getsockname(k->s, reinterpret_cast<sockaddr*>(&bound), &size);
        Log("open " + std::to_string(id) + ": udp on " + Text(bound.sin_addr.s_addr, ntohs(bound.sin_port)));
        k->connected = true;
        return Return(c, 0);
    }
    if (k->type == kActive) {
        k->s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (BindAddress() != INADDR_ANY) {
            const sockaddr_in a = Address(BindAddress(), port);
            bind(k->s, reinterpret_cast<const sockaddr*>(&a), sizeof a);
        }
        NonBlocking(k->s);
        const sockaddr_in to = Address(k->remote_ip, k->remote_port);
        connect(k->s, reinterpret_cast<const sockaddr*>(&to), sizeof to);
        Log("open " + std::to_string(id) + ": tcp to " + Text(k->remote_ip, k->remote_port));
        return Return(c, 0);
    }
    static_assert(kPassive == 2, "what is left: a passive socket, waiting for its peer");
    k->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int on = 1;
    setsockopt(k->listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    const sockaddr_in a = Address(BindAddress(), port);
    if (bind(k->listener, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || listen(k->listener, 4) != 0) {
        Log("open " + std::to_string(id) + ": tcp listen on port " + std::to_string(port) + " failed");
        return Return(c, kError);
    }
    NonBlocking(k->listener);
    Log("open " + std::to_string(id) + ": tcp listening on port " + std::to_string(port));
    return Return(c, 0);
}

// A passive socket's peer, if it came; an active one's connection, if done.
bool Ready(Socket& k) {
    if (k.connected) return true;
    if (k.listener != INVALID_SOCKET) {
        sockaddr_in from{};
        int size = sizeof from;
        const SOCKET s = accept(k.listener, reinterpret_cast<sockaddr*>(&from), &size);
        if (s == INVALID_SOCKET) return false;
        NonBlocking(s);
        k.s = s;
        k.remote_ip = from.sin_addr.s_addr;
        k.remote_port = ntohs(from.sin_port);
        k.connected = true;
        Log("accepted " + Text(k.remote_ip, k.remote_port));
        return true;
    }
    if (k.s == INVALID_SOCKET) return false;
    fd_set w;
    FD_ZERO(&w);
    FD_SET(k.s, &w);
    timeval zero{};
    if (select(0, nullptr, &w, nullptr, &zero) == 1) k.connected = true;
    return k.connected;
}

// 3: Close(id, ms)
void Close(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    Socket* k = Find(id);
    if (!k) return Return(c, kNoConnection);
    if (k->s != INVALID_SOCKET) closesocket(k->s);
    if (k->listener != INVALID_SOCKET) closesocket(k->listener);
    Sockets().erase(id);
    if (Tell(3)) Log("close " + std::to_string(id));
    Return(c, 0);
}

s32 NothingYet(s32 ms) { return ms == 0 ? 0 : kTimeout; }

// 4: Recv(id, data, length, &flags, ms)
void Recv(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    const u32 data = Arg(c, 1), length = Arg(c, 2), flags = Arg(c, 3);
    const s32 ms = s32(Arg(c, 4));
    Socket* k = Find(id);
    if (!k) return Return(c, kNoConnection);
    if (flags) generated::Write32(flags, 0);
    if (!Ready(*k)) return Return(c, NothingYet(ms));
    std::string buffer(length ? length : 1, '\0');
    const int got = recv(k->s, buffer.data(), int(length), 0);
    if (Tell(4)) Log("recv " + std::to_string(id) + ": " + std::to_string(got));
    if (got < 0) return Return(c, WouldBlock() ? NothingYet(ms) : kError);
    for (int i = 0; i < got; ++i) generated::Write8(data + u32(i), u8(buffer[size_t(i)]));
    if (got == 0 && flags) generated::Write32(flags, 4);   // closed by the other end
    Return(c, got);
}

// 5: Send(id, data, length, &flags, ms)
void Send(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    const u32 data = Arg(c, 1), length = Arg(c, 2);
    const s32 ms = s32(Arg(c, 4));
    Socket* k = Find(id);
    if (!k) return Return(c, kNoConnection);
    if (!Ready(*k)) return Return(c, NothingYet(ms));
    std::string buffer(length, '\0');
    for (u32 i = 0; i < length; ++i) buffer[i] = char(generated::ReadU8(data + i));
    int sent;
    if (k->type == kDgram || k->type == kRaw) {
        if (k->type == kRaw) return Return(c, s32(length));       // pings go nowhere
        const sockaddr_in to = Address(k->remote_ip, k->remote_port);
        sent = sendto(k->s, buffer.data(), int(length), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
        if (Tell(5)) Log("send " + std::to_string(id) + ": " + std::to_string(length) + " bytes to " +
                         Text(k->remote_ip, k->remote_port) + (sent < 0 ? " failed" : ""));
    } else {
        sent = send(k->s, buffer.data(), int(length), 0);
    }
    Summary();
    if (sent < 0) {
        ++Counts().errors;
        return Return(c, WouldBlock() ? NothingYet(ms) : kError);
    }
    ++Counts().sent;
    Counts().sent_bytes += u32(sent);
    Return(c, sent);
}

// 13: RecvFrom(id, data, length, &flags, &address, &port, ms)
void RecvFrom(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    const u32 data = Arg(c, 1), length = Arg(c, 2), flags = Arg(c, 3), address = Arg(c, 4), port = Arg(c, 5);
    const s32 ms = s32(Arg(c, 6));
    Socket* k = Find(id);
    if (!k || k->s == INVALID_SOCKET) return Return(c, kNoConnection);
    if (flags) generated::Write32(flags, 0);
    std::string buffer(length ? length : 1, '\0');
    sockaddr_in from{};
    int size = sizeof from;
    const int got = recvfrom(k->s, buffer.data(), int(length), 0, reinterpret_cast<sockaddr*>(&from), &size);
    Summary();
    if (got < 0) {
        const int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET) {
            ++Counts().empty;
            return Return(c, NothingYet(ms));
        }
        ++Counts().errors;
        return Return(c, kError);
    }
    ++Counts().got;
    Counts().got_bytes += u32(got);
    Dump("<-", from.sin_addr.s_addr, ntohs(from.sin_port), buffer.data(), got);
    for (int i = 0; i < got; ++i) generated::Write8(data + u32(i), u8(buffer[size_t(i)]));
    if (address) {
        for (u32 i = 0; i < 16; i += 4) generated::Write32(address + i, 0);
        generated::Write32(address + 4, GuestIp(from.sin_addr.s_addr));
    }
    if (port) generated::Write32(port, ntohs(from.sin_port));
    if (Tell(13)) Log("recvfrom " + std::to_string(id) + ": " + std::to_string(got) + " bytes from " +
                      Text(from.sin_addr.s_addr, ntohs(from.sin_port)));
    Return(c, got);
}

// 14: SendTo(id, data, length, &flags, &address, port, ms)
void SendTo(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    const u32 data = Arg(c, 1), length = Arg(c, 2), address = Arg(c, 4), port = Arg(c, 5);
    const s32 ms = s32(Arg(c, 6));
    Socket* k = Find(id);
    if (!k || k->s == INVALID_SOCKET) return Return(c, kNoConnection);
    if (k->type == kRaw) return Return(c, s32(length));
    std::string buffer(length, '\0');
    for (u32 i = 0; i < length; ++i) buffer[i] = char(generated::ReadU8(data + i));
    const u32 ip = HostIp(u32(generated::ReadU32(address + 4)));
    const sockaddr_in to = Address(ip, port);
    const int sent = sendto(k->s, buffer.data(), int(length), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    Dump("->", ip, port, buffer.data(), int(length));
    if (Tell(14)) Log("sendto " + std::to_string(id) + ": " + std::to_string(length) + " bytes to " + Text(ip, port) +
                      (sent < 0 ? " failed (" + std::to_string(WSAGetLastError()) + ")" : ""));
    Summary();
    if (sent < 0) {
        ++Counts().errors;
        return Return(c, WouldBlock() ? NothingYet(ms) : kError);
    }
    ++Counts().sent;
    Counts().sent_bytes += u32(sent);
    Return(c, sent);
}

// 15: Abort(id, flags)
void Abort(lifted::Context& c) { Return(c, 0); }

// 19: Control(id, code, buffer, length). Code 1: the connection's information
// (0x4C bytes): local address +0x10 (IPv4 at +0x14), local port +0x20, remote
// address +0x24 (IPv4 at +0x28), remote port +0x34, state +0x38 (10 established).
void Control(lifted::Context& c) {
    const s32 id = s32(Arg(c, 0));
    const u32 code = Arg(c, 1), buffer = Arg(c, 2), length = Arg(c, 3);
    Socket* k = Find(id);
    if (!k) return Return(c, kNoConnection);
    if (code != 1 || length < 0x3C) {
        if (Tell(19)) Log("control " + std::to_string(id) + " code " + std::to_string(code) + ": nothing done");
        return Return(c, 0);
    }
    for (u32 i = 0; i < length; i += 4) generated::Write32(buffer + i, 0);
    sockaddr_in local{};
    int size = sizeof local;
    if (k->s != INVALID_SOCKET) getsockname(k->s, reinterpret_cast<sockaddr*>(&local), &size);
    generated::Write32(buffer + 0x14, GuestIp(local.sin_addr.s_addr));
    generated::Write32(buffer + 0x20, ntohs(local.sin_port));
    generated::Write32(buffer + 0x28, GuestIp(k->remote_ip));
    generated::Write32(buffer + 0x34, k->remote_port);
    generated::Write32(buffer + 0x38, Ready(*k) ? 10 : 2);
    Return(c, 0);
}

// 20: Poll(entries, count, ms): an entry is {id s32, events s16, revents s16};
// events 1 readable, 4 writable (as poll's). Answers at once.
void Poll(lifted::Context& c) {
    const u32 entries = Arg(c, 0), count = Arg(c, 1);
    s32 ready = 0;
    for (u32 i = 0; i < count; ++i) {
        const u32 e = entries + i * 8;
        const s32 id = s32(generated::ReadU32(e));
        const u16 events = u16(generated::ReadU16(e + 4));
        u16 revents = 0;
        if (Socket* k = Find(id)) {
            const bool up = Ready(*k);
            if (up && k->s != INVALID_SOCKET) {
                fd_set r, w;
                FD_ZERO(&r);
                FD_ZERO(&w);
                FD_SET(k->s, &r);
                FD_SET(k->s, &w);
                timeval zero{};
                if (select(0, &r, &w, nullptr, &zero) > 0) {
                    if (FD_ISSET(k->s, &r)) revents |= 1;
                    if (FD_ISSET(k->s, &w)) revents |= 4;
                }
            }
        } else {
            revents = 0x20;                         // not a socket
        }
        revents &= u16(events | 0x38);
        generated::Write16(e + 6, revents);
        if (revents) ++ready;
    }
    ++Counts().polls;
    Summary();
    if (Tell(20) && ready) Log("poll: " + std::to_string(ready) + " of " + std::to_string(count) + " ready");
    Return(c, ready);
}

}  // namespace

void RegisterLanLibnet() {
    if (!LanEnabled()) return;
    ps2host::Override(0x003D3658u, Create, "LAN: libnet sceInetCreate");
    ps2host::Override(0x003D36C8u, Open, "LAN: libnet sceInetOpen");
    ps2host::Override(0x003D3710u, Close, "LAN: libnet sceInetClose");
    ps2host::Override(0x003D3758u, Recv, "LAN: libnet sceInetRecv");
    ps2host::Override(0x003D3930u, Send, "LAN: libnet sceInetSend");
    ps2host::Override(0x003D3820u, RecvFrom, "LAN: libnet sceInetRecvFrom");
    ps2host::Override(0x003D39E0u, SendTo, "LAN: libnet sceInetSendTo");
    ps2host::Override(0x003D3AC8u, Abort, "LAN: libnet sceInetAbort");
    ps2host::Override(0x003D3B10u, Control, "LAN: libnet sceInetControl");
    ps2host::Override(0x003D3380u, Poll, "LAN: libnet sceInetPoll");
}
