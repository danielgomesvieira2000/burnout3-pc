/*
 * LAN play: Sony's libnet, answered on the EE with the PC's sockets (the
 * engine's ps2host/libnet.h).
 *
 * The race itself goes from player to player (UDP, port 3658), through the
 * game's own BSD-like socket layer (0x00237xxx) on top of Sony's libnet,
 * whose client's wrappers (0x003D3xxx) send each call to the IOP through
 * MSIFRPC (0x003D3158 -> sceSifMCallRpc); the engine answers them here.
 *
 * B3_LAN_IP=<address>: this instance's address, for its sockets and what it
 * tells the others (two instances on one PC: 127.0.0.1 and 127.0.0.2).
 * B3_LAN_DUMP=1: each datagram in the log. B3_LAN_DEBUG=1: the calls.
 */

#include "ps2host/host.h"
#include "ps2host/libnet.h"

bool LanEnabled();                 // iop_servers.cpp
bool LanDebug();                   // lan.cpp: B3_LAN_DEBUG=1

void RegisterLanLibnet() {
    if (!LanEnabled()) return;
    ps2host::libnet::Client client;
    client.create = 0x003D3658u;
    client.open = 0x003D36C8u;
    client.close = 0x003D3710u;
    client.recv = 0x003D3758u;
    client.send = 0x003D3930u;
    client.recv_from = 0x003D3820u;
    client.send_to = 0x003D39E0u;
    client.abort = 0x003D3AC8u;
    client.control = 0x003D3B10u;
    client.poll = 0x003D3380u;
    ps2host::libnet::Options options;
    options.address_env = "B3_LAN_IP";
    options.dump_env = "B3_LAN_DUMP";
    options.debug = LanDebug;
    ps2host::libnet::Register(client, options);
}
