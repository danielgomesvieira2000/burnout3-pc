/*
 * LAN play: the lobby, inside the game - EA's redirector and lobby ("slave")
 * as the online flow needs them, so that no server runs apart.
 *
 * Who serves: once a network is taken in the game's list of network
 * configurations (lan.cpp, ChooseNetwork), the game asks the LAN (a UDP
 * broadcast, port 21801) whether a lobby is up. One that answers is used;
 * else this game starts its own and is the LAN's lobby from then on.
 * PS2HOST_INET_HOST=<address> set by hand skips all of it (two instances on
 * one PC, a lobby elsewhere): the lobby is there.
 *
 * The game finds the lobby by name; the engine's inet answers every name with
 * PS2HOST_INET_HOST (iop_inet.cpp, LanHost), which is set here before the
 * online flow looks the name up.
 *
 * Messages (ProtoAries): a 12-byte header - command (4 bytes), code (4 bytes,
 * zero in plain replies), big-endian length including the header - then
 * KEY=value pairs, one per line, and a NUL. Ports: redirector TCP 21800,
 * lobby TCP 10901, discovery UDP 21801.
 *
 * Networks: the question goes out on every adapter (Discover), the answer
 * names this PC's address on the asker's network (Discovery), and the
 * serving player is handed to each other player at its address on that
 * player's network (Game::AddressFor). A group that shares one network -
 * the real LAN, ZeroTier, Radmin - finds itself with nothing set by hand.
 *
 * The network can also be chosen, in the game's list of network
 * configurations (lan.cpp: AUTO, then each of this PC's networks): the
 * question, the answer and the race then go through that one only - for a
 * player on two networks (the real LAN and a VPN), whose first answer could
 * otherwise come over the network the others are not on.
 *
 * TODO:
 * - Mixed groups (one player only on the real LAN, another only on a VPN,
 *   the host on both): the race is peer to peer (lan_libnet.cpp, UDP 3658)
 *   and those two have no path between them. Hand each player stand-in
 *   addresses for the others and let lan_libnet.cpp send directly where a
 *   real address answers, else through the host as a relay.
 * - "Join by address" in the game's menus, in place of PS2HOST_INET_HOST,
 *   for VPNs that carry no broadcast at all.
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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
#include <iphlpapi.h>
#endif

bool LanDebug();                   // lan.cpp: B3_LAN_DEBUG=1
bool LanAddressByHand();           // lan.cpp: B3_LAN_IP set before the game started

namespace {

#ifdef _WIN32

constexpr unsigned short kRedirectorPort = 21800, kLobbyPort = 10901, kDiscoveryPort = 21801;
constexpr const char* kStamp = "2004.9.7 12:00:00";
constexpr int kRoomId = 1;
std::string g_room_name = "LAN";            // the room: the serving player's name (LanPrepareLobby)
constexpr const char kQuestion[] = "B3LAN?1", kAnswer[] = "B3LAN!1";

using Pairs = std::map<std::string, std::string>;
using List = std::vector<std::string>;

void Log(const std::string& text) {
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count() % 1000000;
    std::fprintf(stderr, "[lobby] %06lld %s\n", ms, text.c_str());
}

std::string Dotted(const in_addr& a) {
    char text[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &a, text, sizeof text);
    return text;
}

/* This PC's IPv4 networks: each adapter that is up (the real LAN, ZeroTier,
   Radmin...), its address, mask and name; the loopback and addresses with
   no network behind them (169.254.x.x) left out. */
struct Network {
    in_addr address, mask;
    std::string name;               // Windows' name for the adapter ("ZeroTier One [8056c2e2...]")
    bool Holds(const in_addr& a) const { return (a.s_addr & mask.s_addr) == (address.s_addr & mask.s_addr); }
};

std::vector<Network> Networks() {
    std::vector<Network> out;
    ULONG size = 16384;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && result == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) return out;
    for (const IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        std::string name;
        for (const wchar_t* w = a->FriendlyName; w && *w; ++w) name += *w >= 32 && *w < 127 ? char(*w) : '?';
        for (const IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const in_addr ip = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
            if ((ntohl(ip.s_addr) >> 16) == 0xA9FEu) continue;
            const unsigned bits = u->OnLinkPrefixLength;
            in_addr mask{};
            mask.s_addr = bits == 0 ? 0 : htonl(bits >= 32 ? 0xFFFFFFFFu : ~0u << (32 - bits));
            out.push_back({ip, mask, name});
        }
    }
    return out;
}

std::string Get(const Pairs& p, const std::string& key, const std::string& otherwise = "") {
    const auto it = p.find(key);
    return it == p.end() ? otherwise : it->second;
}

std::string Pack(const std::string& command, const List& pairs, const char code[4] = "\0\0\0") {
    std::string body;
    for (const std::string& p : pairs) body += p + "\n";
    if (!pairs.empty()) body += '\0';
    std::string out(command.substr(0, 4));
    out.resize(4, '\0');
    out.append(code, 4);
    const unsigned length = unsigned(12 + body.size());
    const char size[4] = {char(length >> 24), char(length >> 16), char(length >> 8), char(length)};
    out.append(size, 4);
    return out + body;
}

Pairs Parse(const std::string& body) {
    Pairs pairs;
    std::string item;
    auto take = [&] {
        const auto eq = item.find('=');
        const std::string key = item.substr(0, eq);
        if (!key.empty()) pairs[key] = eq == std::string::npos ? "" : item.substr(eq + 1);
        item.clear();
    };
    for (const char ch : body) {
        if (ch == '\0') break;
        if (ch == '\n' || ch == '\t') take();
        else item += ch;
    }
    take();
    return pairs;
}

std::string Show(const List& pairs) {
    if (pairs.empty()) return "(empty)";
    std::string out;
    for (const std::string& p : pairs) out += (out.empty() ? "" : " | ") + p;
    return out.substr(0, 300);
}

bool SendAll(SOCKET s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(s, data.data() + sent, int(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

/* Reads messages until the connection ends: (command, code, body) each. */
template <class Handle>
void Messages(SOCKET s, const char* name, Handle handle) {
    std::string buffer;
    char chunk[65536];
    for (;;) {
        const int got = recv(s, chunk, sizeof chunk, 0);
        if (got <= 0) return;
        buffer.append(chunk, size_t(got));
        while (buffer.size() >= 12) {
            const unsigned length = unsigned(u_char(buffer[8])) << 24 | unsigned(u_char(buffer[9])) << 16 |
                                    unsigned(u_char(buffer[10])) << 8 | unsigned(u_char(buffer[11]));
            if (length < 12 || length > (1u << 20)) {
                Log(std::string(name) + ": bad length " + std::to_string(length));
                return;
            }
            if (buffer.size() < length) break;
            handle(buffer.substr(0, 4).c_str(), buffer.substr(12, length - 12));
            buffer.erase(0, length);
        }
    }
}

struct Game;

struct User {
    SOCKET s = INVALID_SOCKET;
    std::string address;            // as the lobby sees it: what the others reach
    std::string here;               // this PC's address on that player's network
    int id = 0;
    std::string name, pers;
    bool in_room = false;
    std::shared_ptr<Game> game;     // the game it is in
    std::string params;             // its USERPARAMS there (the car...)
    std::mutex send_lock;

    void Send(const std::string& command, const List& pairs = {}, const char code[4] = "\0\0\0") {
        std::lock_guard<std::mutex> hold(send_lock);
        SendAll(s, Pack(command, pairs, code));
        if (command[0] != '+' && LanDebug()) Log(pers + " -> " + command + " " + Show(pairs));
    }
    List Record() const;
    void Who() {
        List r = Record();
        r.insert(r.end(), {"R=" + g_room_name, "RI=" + std::to_string(kRoomId), "RF=C", "RT=1"});
        Send("+who", r);
    }
};

struct Game {
    int ident = 0;
    std::shared_ptr<User> host;
    Pairs settings;
    std::vector<std::shared_ptr<User>> players;
    bool started = false;
    unsigned seed = 0;

    std::string Setting(const std::string& key, const std::string& otherwise) const {
        return Get(settings, key, otherwise);
    }
    /* The game as the client parses it (0x004090E8); SELF is the viewer. */
    /* Where VIEWER reaches P for the race: the address P came from, but the
       serving PC's own player (which came from this PC's address) is reached
       at this PC's address on the viewer's network - its LAN address for one
       beside it, its VPN address for one on ZeroTier or Radmin. */
    static std::string AddressFor(const User& p, const User& viewer) {
        return p.address == p.here && !viewer.here.empty() ? viewer.here : p.address;
    }
    List Record(const User& viewer) const {
        List out = {"IDENT=" + std::to_string(ident), "NAME=" + Setting("NAME", host->pers), "SELF=" + viewer.pers,
                    "HOST=" + host->pers, "PARAMS=" + Setting("PARAMS", ""), "ROOM=" + std::to_string(kRoomId),
                    "CUSTFLAGS=" + Setting("CUSTFLAGS", "0"), "SYSFLAGS=" + Setting("SYSFLAGS", "0"),
                    "COUNT=" + std::to_string(players.size()), "MINSIZE=" + Setting("MINSIZE", "2"),
                    "MAXSIZE=" + Setting("MAXSIZE", "6"), "NUMPART=1", "SEED=" + std::to_string(seed),
                    "WHEN=" + std::string(kStamp)};
        for (size_t i = 0; i < players.size(); ++i) {
            const std::string n = std::to_string(i);
            const User& p = *players[i];
            out.insert(out.end(), {"OPID" + n + "=" + std::to_string(p.id), "OPPO" + n + "=" + p.pers,
                                   "ADDR" + n + "=" + AddressFor(p, viewer), "LADDR" + n + "=" + AddressFor(p, viewer),
                                   "MADDR" + n + "=",
                                   "OPPART" + n + "=0", "OPPARAM" + n + "=" + p.params, "OPFLAG" + n + "=0"});
        }
        out.insert(out.end(), {"PARTSIZE0=" + Setting("MAXSIZE", "6"), "PARTPARAMS0="});
        return out;
    }
};

/* The player as the client parses it (0x00409618). G: its game. */
List User::Record() const {
    return {"I=" + std::to_string(id), "N=" + pers, "M=" + name, "F=", "A=" + address, "S=", "X=",
            "G=" + std::to_string(game ? game->ident : 0)};
}

std::recursive_mutex g_lock;                // one lock for the whole lobby: it is tiny
std::vector<std::shared_ptr<User>> g_users;
std::map<int, std::shared_ptr<Game>> g_games;
int g_next_user = 1, g_next_game = 1;

std::vector<std::shared_ptr<User>> InRoom() {
    std::vector<std::shared_ptr<User>> out;
    for (const auto& u : g_users)
        if (u->in_room) out.push_back(u);
    return out;
}

/* A game's news to everyone in the room, each with its own SELF: +agm the
   room's games (a creator or a joiner waits to see itself there,
   0x0033B5B0), +gam the ACTIVE RACES screen's. */
void Announce(const Game& game) {
    for (const auto& u : InRoom()) {
        u->Send("+agm", game.Record(*u));
        u->Send("+gam", game.Record(*u));
    }
}

void Gone(const Game& game, const std::vector<std::shared_ptr<User>>& to) {
    for (const auto& u : to) {
        u->Send("+agm", {"IDENT=" + std::to_string(game.ident)});      // no NAME: the game is gone
        u->Send("+gam", {"IDENT=" + std::to_string(game.ident)});
    }
}

void RemoveGame(const std::shared_ptr<Game>& game) {
    g_games.erase(game->ident);
    Gone(*game, InRoom());
    for (const auto& p : game->players) {
        p->game = nullptr;
        p->Who();
    }
}

void LeaveGame(User& user) {
    const std::shared_ptr<Game> game = user.game;
    if (!game) return;
    if (game->started) {
        // The race is peer to peer: players leave the lobby at the start. Any
        // news to the others (COUNT, or the game gone) changes their player
        // list, and the client then resets the race's link (0x0026F150). The
        // roster stays frozen; the game goes quietly with its last player.
        user.game = nullptr;
        for (const auto& p : game->players)
            if (p->game == game) return;
        g_games.erase(game->ident);
        std::vector<std::shared_ptr<User>> others;
        for (const auto& u : InRoom()) {
            bool player = false;
            for (const auto& p : game->players) player = player || p == u;
            if (!player) others.push_back(u);
        }
        Gone(*game, others);
        game->players.clear();
        return;
    }
    if (&user == game->host.get()) {
        RemoveGame(game);
        return;
    }
    for (auto it = game->players.begin(); it != game->players.end(); ++it)
        if (it->get() == &user) {
            game->players.erase(it);
            break;
        }
    user.game = nullptr;
    user.params.clear();
    user.Who();
    Announce(*game);
}

std::shared_ptr<Game> FindGame(const Pairs& pairs) {
    const std::string name = Get(pairs, "NAME");
    for (const auto& [ident, game] : g_games)
        if (!name.empty() && game->Setting("NAME", game->host->pers) == name) return game;
    const auto it = g_games.find(std::atoi(Get(pairs, "IDENT", "0").c_str()));
    return it == g_games.end() ? nullptr : it->second;
}

void TellMembers(const Game& game, const User& except) {
    for (const auto& p : game.players)
        if (p.get() != &except) p->Send("+mgm", game.Record(*p));
}

std::string WithStarted(const std::string& flags) {
    return std::to_string(unsigned(std::strtoul(flags.c_str(), nullptr, 10)) | 0x80000u);
}

/* The lobby's commands: each replies itself. */
void Command(const std::shared_ptr<User>& self, const std::string& command, const Pairs& pairs) {
    User& user = *self;
    if (command == "addr") {
        user.Send("skey", {"SKEY=$37940faf2a8d1381a3b7d0d2f570e6a7"});
    } else if (command == "auth") {
        user.name = user.pers = Get(pairs, "NAME", user.name);
        user.Send("auth", {"TOS=1", "NAME=" + user.name, "ADDR=" + user.address, "MAIL=lan@localhost",
                           "BORN=19800101", "GEND=M", "FROM=US", "LANG=en", "SPAM=NN", "PERSONAS=" + user.name,
                           "LAST=" + std::string(kStamp)});
    } else if (command == "acct") {
        user.Send("acct", {"NAME=" + Get(pairs, "NAME", user.name), "PERSONAS=", "AGE=24"});
    } else if (command == "cper") {
        user.Send("cper", {"PERS=" + Get(pairs, "PERS", user.name), "NAME=" + user.name});
    } else if (command == "pers") {
        user.pers = Get(pairs, "PERS", user.pers);
        user.Send("pers", {"NAME=" + user.name, "PERS=" + user.pers, "LAST=" + std::string(kStamp),
                           "PLAST=" + std::string(kStamp), "LKEY=3fcf27540c92935b0a66fd3b0000283c"});
        user.Who();
        user.Send("+rom", {"I=" + std::to_string(kRoomId), "N=" + g_room_name,
                           "H=" + g_room_name, "D=" + g_room_name, "F=C", "A=" + user.address,
                           "T=" + std::to_string(InRoom().size()), "L=50", "C=0", "P=0"});
    } else if (command == "sviw") {
        // The stats' view: N columns, then comma-separated lists (_LobbyStatbookViewCb).
        user.Send("sviw", {"N=1", "NAMES=0", "DESCS=Score", "PARAMS=0", "WIDTHS=10"});
    } else if (command == "move") {
        // Burnout 3 names the room by its IDENT; 0 or nothing leaves it.
        const std::string ident = Get(pairs, "IDENT", "0");
        if ((ident.empty() || ident == "0") && Get(pairs, "NAME").empty()) {
            LeaveGame(user);
            user.in_room = false;
            user.Send("move", {"IDENT=0", "NAME=", "COUNT=0"});
        } else {
            user.in_room = true;
            const auto room = InRoom();
            user.Send("move", {"IDENT=" + std::to_string(kRoomId), "NAME=" + g_room_name,
                               "COUNT=" + std::to_string(room.size()), "FLAGS=C", "LIDENT=0", "LCOUNT=0"});
            for (const auto& other : room) {
                user.Send("+usr", other->Record());
                if (other != self) other->Send("+usr", user.Record());
            }
            for (const auto& [i, game] : g_games) user.Send("+agm", game->Record(user));
        }
        const auto room = InRoom();
        for (const auto& u : room)
            u->Send("+pop", {"Z=" + std::to_string(kRoomId) + "/" + std::to_string(room.size())});
    } else if (command == "gcre") {
        LeaveGame(user);
        auto game = std::make_shared<Game>();
        game->ident = g_next_game++;
        game->host = self;
        for (const char* key : {"NAME", "PASS", "PARAMS", "MINSIZE", "MAXSIZE", "SYSFLAGS", "CUSTFLAGS"})
            if (pairs.count(key)) game->settings[key] = pairs.at(key);
        game->players = {self};
        game->seed = unsigned(std::time(nullptr)) & 0x7FFFFFFFu;
        g_games[game->ident] = game;
        user.game = game;
        user.params.clear();
        user.Send("gcre", game->Record(user));
        // The creator waits for its game, with itself among the players, in
        // the room's list (0x0033B5B0) and for its own G (0x0033BAE0).
        Announce(*game);
        user.Who();
    } else if (command == "gsea") {
        // A page of the room's games: COUNT, then the games themselves as +gam.
        std::vector<std::shared_ptr<Game>> listed;
        for (const auto& [i, game] : g_games)
            if (!game->started) listed.push_back(game);
        user.Send("gsea", {"COUNT=" + std::to_string(listed.size())});
        for (const auto& game : listed) user.Send("+gam", game->Record(user));
    } else if (command == "gjoi") {
        const auto game = FindGame(pairs);
        if (!game || game->started ||
            int(game->players.size()) >= std::atoi(game->Setting("MAXSIZE", "6").c_str())) {
            user.Send("gjoi", {}, "full");      // an error: a code in place of zero
            return;
        }
        LeaveGame(user);
        game->players.push_back(self);
        user.game = game;
        user.params = Get(pairs, "USERPARAMS");
        user.Send("gjoi", game->Record(user));
        user.Who();
        Announce(*game);
        TellMembers(*game, user);
    } else if (command == "gset") {
        const auto game = user.game;
        if (!game) {
            user.Send("gset");
            return;
        }
        // Once racing, a player's USERPARAMS (the host's "ready" after gsta)
        // are not reflected: a change of a player's OPPARAM makes the clients
        // reset their link to the others (0x0033BC80 -> 0x0026F150).
        if (pairs.count("USERPARAMS") && !game->started) user.params = pairs.at("USERPARAMS");
        if (self == game->host) {
            for (const char* key : {"PARAMS", "SYSFLAGS", "CUSTFLAGS", "PASS"})
                if (pairs.count(key)) game->settings[key] = pairs.at(key);
            // The host's unlock after gsta keeps the game started.
            if (game->started) game->settings["SYSFLAGS"] = WithStarted(game->Setting("SYSFLAGS", "0"));
        }
        user.Send("gset", game->Record(user));
        Announce(*game);
        TellMembers(*game, user);
    } else if (command == "gget") {
        auto game = FindGame(pairs);
        if (!game) game = user.game;
        user.Send("gget", game ? game->Record(user) : List{});
    } else if (command == "glea") {
        LeaveGame(user);
        user.Send("glea");
    } else if (command == "gdel") {
        if (user.game && user.game->host == self) RemoveGame(user.game);
        user.Send("gdel");
    } else if (command == "gsta") {
        // The host starts the race. SYSFLAGS 0x80000: started (what the
        // client's 0x00338DF0 looks for). An empty reply, then +ses - the game,
        // its SEED, the receiver as SELF - to each player, then the lists.
        const auto game = user.game;
        if (!game || game->host != self) {
            user.Send("gsta");
            return;
        }
        game->started = true;
        game->settings["SYSFLAGS"] = WithStarted(game->Setting("SYSFLAGS", "0"));
        user.Send("gsta");
        for (const auto& p : game->players) p->Send("+ses", game->Record(*p));
        Announce(*game);        // the host needs its game's news (else it unlocks over and over)
    } else if (command == "mesg") {
        // The players' own traffic through the lobby: TEXT (bytes as numbers),
        // ATTR (flags as letters, "G..." for the sender's game). Relayed as
        // +msg, F the flags as they are (no U: the game's own data, not chat).
        user.Send("mesg");
        const std::string attr = Get(pairs, "ATTR"), text = Get(pairs, "TEXT");
        std::vector<std::shared_ptr<User>> targets;
        if (pairs.count("PRIV")) {
            for (const auto& u : g_users)
                if (u->pers == pairs.at("PRIV")) targets.push_back(u);
        } else if (user.game && !attr.empty() && attr[0] == 'G') {
            for (const auto& p : user.game->players)
                if (p != self) targets.push_back(p);
        } else {
            for (const auto& u : InRoom())
                if (u != self) targets.push_back(u);
        }
        for (const auto& t : targets) t->Send("+msg", {"F=" + attr, "T=" + text, "N=" + user.pers});
    } else {
        // Not known: a reply of the same name with the request's own fields.
        // A stats row asked for (a player's profile) is matched by the slot
        // it names - an empty reply left "Retrieving profile data" for ever
        // (_LobbyStatbookRowCb: expected slot 0 got -1). Each told once.
        static std::set<std::string> told;
        List shown, echoed;
        for (const auto& [k, v] : pairs) {
            if (k != "PASS") shown.push_back(k + "=" + v);
            echoed.push_back(k + "=" + v);
        }
        if (told.insert(command).second) Log("not known: " + command + " " + Show(shown) + " - its fields sent back");
        user.Send(command, echoed);
    }
}

SOCKET Listen(unsigned short port, int type) {
    const SOCKET s = socket(AF_INET, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
    if (s == INVALID_SOCKET) return s;
    // Exclusive: a second lobby on the port fails here instead of sharing it.
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || (type == SOCK_STREAM && listen(s, 16) != 0)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* The redirector: the lobby's address - the one this connection reached. */
void Redirector(SOCKET s) {
    sockaddr_in here{};
    int size = sizeof here;
    getsockname(s, reinterpret_cast<sockaddr*>(&here), &size);
    Messages(s, "redir", [&](const char* command, const std::string&) {
        if (std::strncmp(command, "@dir", 4) == 0)
            SendAll(s, Pack("@dir", {"ADDR=" + Dotted(here.sin_addr), "PORT=" + std::to_string(kLobbyPort),
                                     "SESS=1072010288", "MASK=0295f3f70ecb1757cd7001b9a7a5eac8"}));
    });
    closesocket(s);
}

void StopLobby();

void Lobby(SOCKET s, sockaddr_in peer) {
    auto self = std::make_shared<User>();
    {
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        self->s = s;
        self->address = Dotted(peer.sin_addr);
        sockaddr_in here{};
        int size = sizeof here;
        if (getsockname(s, reinterpret_cast<sockaddr*>(&here), &size) == 0) self->here = Dotted(here.sin_addr);
        self->id = g_next_user++;
        self->name = self->pers = "PLAYER" + std::to_string(self->id);
        g_users.push_back(self);
    }
    Log(self->pers + " connected from " + self->address);
    Messages(s, "lobby", [&](const char* raw, const std::string& body) {
        const std::string command(raw, strnlen(raw, 4));
        const Pairs pairs = Parse(body);
        List shown;
        for (const auto& [k, v] : pairs)
            if (k != "PASS") shown.push_back(k + "=" + v);
        if (LanDebug()) Log(self->pers + " <- " + command + " " + Show(shown));
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        Command(self, command, pairs);
    });
    bool host_left = false;
    {
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        // The serving game's own player (it came from this PC's address)
        // leaving, not for a race (players leave the lobby as one starts):
        // back to the menus - the lobby goes with it (StopLobby, below).
        host_left = self->address == self->here && !(self->game && self->game->started);
        LeaveGame(*self);
        for (auto it = g_users.begin(); it != g_users.end(); ++it)
            if (*it == self) {
                g_users.erase(it);
                break;
            }
        const auto room = InRoom();
        for (const auto& u : room)
            u->Send("+pop", {"Z=" + std::to_string(kRoomId) + "/" + std::to_string(room.size())});
        // Closed here unless the lobby closed (StopLobby) and closed it.
        if (self->s != INVALID_SOCKET) closesocket(self->s);
        self->s = INVALID_SOCKET;
    }
    Log(self->pers + " disconnected");
    if (host_left) StopLobby();
}

template <class Handle>
void Accept(SOCKET listener, Handle handle) {
    for (;;) {
        sockaddr_in peer{};
        int size = sizeof peer;
        const SOCKET s = accept(listener, reinterpret_cast<sockaddr*>(&peer), &size);
        if (s == INVALID_SOCKET) return;
        std::thread([=] { handle(s, peer); }).detach();
    }
}

/* Answers the LAN's question "a lobby here?" with the lobby's address as the
   asker reaches it: this PC's on the network the question came from (the
   real LAN's for a player beside it, the VPN's for one on ZeroTier or
   Radmin). An address set by hand (B3_LAN_IP before the game started;
   127.0.0.1 for tests on one PC) is the answer whoever asks. A network
   chosen in the game's list: only its players are answered - the race
   goes out of that network alone, the others make their own lobby. */
void Discovery(SOCKET s, std::string address) {
    const bool fixed = LanAddressByHand() || address.rfind("127.", 0) == 0;
    in_addr chosen{};
    const char* text = std::getenv("B3_LAN_IP");
    const bool one = !fixed && text && *text && inet_pton(AF_INET, text, &chosen) == 1;
    // The answer for ASKER, or "" for none.
    const auto AnswerFor = [&](const in_addr& asker) {
        if (fixed) return std::string(kAnswer) + " " + address;
        for (const Network& n : Networks())
            if (n.Holds(asker)) {
                if (one && n.address.s_addr != chosen.s_addr) break;
                return std::string(kAnswer) + " " + Dotted(n.address);
            }
        if (one) {
            if (LanDebug()) Log("a question from " + Dotted(asker) + ", not on the chosen network: not answered");
            return std::string();
        }
        return std::string(kAnswer) + " " + address;
    };
    for (;;) {
        char data[64];
        sockaddr_in from{};
        int size = sizeof from;
        const int got = recvfrom(s, data, sizeof data, 0, reinterpret_cast<sockaddr*>(&from), &size);
        if (got < 0) {
            if (WSAGetLastError() == WSAECONNRESET) continue;
            return;
        }
        if (got == int(sizeof kQuestion - 1) && std::memcmp(data, kQuestion, sizeof kQuestion - 1) == 0) {
            const std::string answer = AnswerFor(from.sin_addr);
            if (!answer.empty())
                sendto(s, answer.data(), int(answer.size()), 0, reinterpret_cast<sockaddr*>(&from), size);
        }
    }
}

bool g_serving = false;
SOCKET g_listeners[3] = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET};

/* Closes the lobby: its ports (the Accept and Discovery threads end) and its
   players' connections, cut short (no TIME_WAIT left on 10901 to stop the
   next lobby taking the port). */
void StopLobby() {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g_serving) return;
    for (SOCKET& s : g_listeners) {
        if (s != INVALID_SOCKET) closesocket(s);
        s = INVALID_SOCKET;
    }
    for (const auto& u : g_users) {
        std::lock_guard<std::mutex> sending(u->send_lock);
        if (u->s == INVALID_SOCKET) continue;
        const linger cut{1, 0};
        setsockopt(u->s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&cut), sizeof cut);
        closesocket(u->s);
        u->s = INVALID_SOCKET;
    }
    for (const auto& u : g_users) u->game = nullptr;
    g_users.clear();
    g_games.clear();
    g_serving = false;
    Log("the lobby closed");
}

bool StartLobby(const std::string& address) {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    const SOCKET redirector = Listen(kRedirectorPort, SOCK_STREAM);
    const SOCKET lobby = Listen(kLobbyPort, SOCK_STREAM);
    const SOCKET discovery = Listen(kDiscoveryPort, SOCK_DGRAM);
    if (redirector == INVALID_SOCKET || lobby == INVALID_SOCKET || discovery == INVALID_SOCKET) {
        Log("cannot serve: a port is taken (21800, 10901 or UDP 21801) - another lobby on this PC?");
        for (SOCKET s : {redirector, lobby, discovery})
            if (s != INVALID_SOCKET) closesocket(s);
        return false;
    }
    g_listeners[0] = redirector;
    g_listeners[1] = lobby;
    g_listeners[2] = discovery;
    std::thread([=] { Accept(redirector, [](SOCKET s, sockaddr_in) { Redirector(s); }); }).detach();
    std::thread([=] { Accept(lobby, Lobby); }).detach();
    std::thread([=] { Discovery(discovery, address); }).detach();
    Log("serving the LAN: redirector 21800, lobby 10901, discovery UDP 21801");
    return true;
}

/* Asks the LAN for a lobby: the first answer's address, or 0. The question
   goes to 255.255.255.255, which Windows sends out of one adapter only, and
   to each network's own broadcast address, so that it reaches the lobby on
   a VPN (ZeroTier, Radmin...) as well as on the real LAN. */
u_long Discover(int wait_ms) {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
    DWORD timeout = 250;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    // A network chosen (B3_LAN_IP: by hand, or in the game's list): its own
    // broadcast address only, if it is one of this PC's.
    in_addr chosen{};
    const char* text = std::getenv("B3_LAN_IP");
    const bool one = text && inet_pton(AF_INET, text, &chosen) == 1 && (ntohl(chosen.s_addr) >> 24) != 127;
    std::vector<Network> networks = Networks();
    if (one) {
        std::vector<Network> mine;
        for (const Network& n : networks)
            if (n.address.s_addr == chosen.s_addr) mine.push_back(n);
        if (!mine.empty())
            networks = mine;
        else
            Log("B3_LAN_IP " + std::string(text) + " is none of this PC's addresses: asking on every network");
    }
    std::vector<u_long> targets;
    if (!one) targets.push_back(htonl(INADDR_BROADCAST));
    for (const Network& n : networks) {
        const u_long broadcast = n.address.s_addr | ~n.mask.s_addr;
        if (n.mask.s_addr != INADDR_BROADCAST && std::find(targets.begin(), targets.end(), broadcast) == targets.end()) {
            targets.push_back(broadcast);
            Log("asking for a lobby on " + Dotted(n.address) + "'s network");
        }
    }
    if (targets.empty()) targets.push_back(htonl(INADDR_BROADCAST));
    u_long found = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    while (!found && std::chrono::steady_clock::now() < until) {
        for (const u_long target : targets) {
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons(kDiscoveryPort);
            to.sin_addr.s_addr = target;
            sendto(s, kQuestion, int(sizeof kQuestion - 1), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
        }
        char data_in[64] = {};
        sockaddr_in from{};
        int size = sizeof from;
        const int got = recvfrom(s, data_in, sizeof data_in - 1, 0, reinterpret_cast<sockaddr*>(&from), &size);
        if (got >= int(sizeof kAnswer - 1) && std::memcmp(data_in, kAnswer, sizeof kAnswer - 1) == 0) {
            // The address the answer names; else the one it came from.
            in_addr named{};
            const char* text = data_in + sizeof kAnswer - 1;
            found = *text == ' ' && inet_pton(AF_INET, text + 1, &named) == 1 ? named.s_addr : from.sin_addr.s_addr;
        }
    }
    closesocket(s);
    return found;
}

#endif

}  // namespace

/* This PC's networks for the game's list of network configurations
   (lan.cpp): each one's address and its adapter's name. */
std::vector<std::pair<std::string, std::string>> LanNetworkList() {
    std::vector<std::pair<std::string, std::string>> out;
#ifdef _WIN32
    for (const Network& n : Networks()) out.emplace_back(Dotted(n.address), n.name);
#endif
    return out;
}

/* LAN left (back in MULTIPLAYER or the main menu, overrides.cpp): the
   lobby this game served, if any, closed. */
void LanStopLobby() {
#ifdef _WIN32
    StopLobby();
#endif
}

std::string LanLocalAddressText();      // lan.cpp: this PC's address on the LAN
std::string LanPlayerName();            // lan.cpp: this PC's player, as the lobby knows it

/* A network taken in the game's list of network configurations (lan.cpp,
   ChooseNetwork; else when the lobby's name is about to be looked up):
   makes sure there is a lobby and points the game at it (PS2HOST_INET_HOST). */
void LanPrepareLobby() {
#ifdef _WIN32
    static bool fixed = std::getenv("PS2HOST_INET_HOST") != nullptr && !g_serving;
    if (fixed) return;          // set by hand: the lobby is there
    // A lobby this game served before (LAN taken again, maybe on another
    // network): closed, and the LAN asked again.
    StopLobby();
    if (const u_long ip = Discover(1200)) {
        in_addr a{};
        a.s_addr = ip;
        _putenv_s("PS2HOST_INET_HOST", Dotted(a).c_str());
        Log("a lobby answered at " + Dotted(a));
        return;
    }
    // This game reaches its own lobby by its LAN address: the address the
    // lobby hands the others for the race is the one each player came from.
    const std::string here = LanLocalAddressText();
    // The room the others see in LOBBY SELECT: the serving player's name.
    g_room_name = LanPlayerName();
    if (!StartLobby(here)) return;
    g_serving = true;
    _putenv_s("PS2HOST_INET_HOST", here.c_str());
#endif
}
