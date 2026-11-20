#pragma once

// The hosts (piccolos) this phone knows and the network to them, on
// frontends-common's frontend_net: this device's identity, the hosts it
// paired with, LAN discovery, pairing with the 6-digit code and connecting.
// Polled from the app's loop (hosts_update), never blocks.
//
// Files, in piccoletto's settings folder (inside the app's own folder on
// Android, not shared storage):
//   net_identity.cfg  this phone's key (0600). Copying it makes another
//                     device this phone; losing it means pairing again.
//   net_hosts.cfg     the hosts paired with (net_trust: key, name)
//   hosts.cfg         what we last saw of each (address, access level), by key
//
// While connected: the ping (ENet's round trip, jitter, loss), read every
// 5 seconds; a speed test when a connection is made (rising rates, down then
// up, until the loss passes 2%; needs STREAM access; once per run of the app
// for each host), kept per host (HOME shows it; the stream's quality later).
//
// FORGET unpairs on both sides when the host can be reached (it's told and
// forgets this phone too); offline, only here. A host that unpairs us is
// dropped from the list.
//
// A discovered host is matched to a paired one by its last address: the
// discovery reply has no key (a fingerprint in it would match after an
// address change; to talk over with the netcode side). A host is "online"
// while it keeps answering discovery.

#include <cstdint>
#include <string>
#include <vector>

#include "net_client.h"
#include "net_host.h"
#include "net_input.h"
#include "net_speed.h"
#include "screens.h"   // LinkShown
#include "settings_file.h"

constexpr int32_t HOSTS_MAX_FOUND = 16;

struct KnownHost
{
    uint8_t        key[NET_KEY_SIZE] = {};
    std::string    name;
    NetAddress     address;                 // last seen; port 0 = never
    NetAccessLevel access = NetAccessLevel::gamepad;
    bool           online = false;          // answering discovery now
    uint32_t       down_kbps = 0;           // last speed test: what got through, host -> phone (0 = never measured)
    uint32_t       up_kbps = 0;             // phone -> host
    bool           measured = false;        // speed-tested since the app started
};


// The speed test running on the connection (see the top).
struct HostsSpeedTest
{
    bool              running = false;
    bool              waiting = false;      // a request is out, its report not in yet
    NetSpeedDirection direction = NetSpeedDirection::down;
    uint32_t          step = 0;             // into the rates
    uint32_t          next_id = 1;
    uint32_t          best_kbps = 0;        // the best rate that got through, this direction
    double            asked_at = 0.0;       // a report that never comes ends the test
    NetSpeedReceiver  receiver;             // down
    NetSpeedSender    sender;               // up
};

struct FoundHost
{
    NetAddress address;                     // where its ENet listens
    char       name[NET_DISCOVERY_MAX_NAME + 1] = {};
    double     seen_at = 0.0;               // seconds
    int32_t    known = -1;                  // the paired host at that address, or -1
};

enum class PairStatus
{
    idle,
    busy,      // connecting / checking the code
    paired,    // `paired_index` is the new host
    failed,    // `refusal` says why
};

enum class LinkStatus
{
    idle,
    connecting,
    ready,     // connected to `linked`
    failed,    // `refusal` says why
};

struct Hosts
{
    bool                   ok = false;          // networking runs (false: no identity, no socket)
    NetClient              client;
    NetHost                discovery;           // our own client host for discovery replies
    std::vector<KnownHost> known;               // the same order as client.trust.devices
    FoundHost              found[HOSTS_MAX_FOUND];
    int32_t                found_count = 0;
    double                 next_discovery = 0.0;
    std::string            info_path;           // hosts.cfg
    SettingsFile           info;

    PairStatus             pair = PairStatus::idle;
    int32_t                paired_index = -1;
    LinkStatus             link = LinkStatus::idle;
    int32_t                linked = -1;         // the known host connecting / connected
    NetAddress             connecting_to;       // the address of the connection running (kept when it's ready)
    bool                   connect_pending = false;     // `linked` is connected to once the last connection has closed
    HostsSpeedTest         speed;
    bool                   has_stats = false;   // `stats` is the ready connection's
    NetLinkStats           stats;
    double                 next_stats = 0.0;    // when to read them again (every 5 s)
    int32_t                pending_unpair = -1; // FORGET: connecting to tell this host, then forget it
    NetRetroPadSender      pad_sender;          // this phone's pad 0, to the connected host
    int8_t                 pad_port = -1;       // the port pad 0 plays on there (controllers), -1 = none
    uint32_t               next_request = 1;    // ids for requests to the host
    NetClientRefusal       refusal = NetClientRefusal::none;
    bool                   refusal_still_known = false;   // pairing failed because the host still lists this phone
};

// `settings_dir`: where the files above go. False (and networking off, the
// app runs on) when there's no identity or no socket.
bool hosts_create(Hosts* hosts, const char* settings_dir, const char* phone_name);
void hosts_destroy(Hosts* hosts);

// Once per frame: discovery every few seconds, the client's events.
void hosts_update(Hosts* hosts, double now);

// This phone's name, sent to the hosts at every connection.
void hosts_set_name(Hosts* hosts, const char* phone_name);

// Unpaired hosts found on the network, in order: the i-th one.
int32_t hosts_unpaired_count(const Hosts* hosts);
const FoundHost* hosts_unpaired(const Hosts* hosts, int32_t index);

// Pair with the host at `address` with the code it shows. Progress in
// hosts->pair. False if it can't start (another connection is running).
bool hosts_pair(Hosts* hosts, NetAddress address, const char* code);
// Connect to a paired host (entering GAMEPAD...) / leave it. The speed test
// runs once it's ready (STREAM access or more; once per run for each host).
bool hosts_connect(Hosts* hosts, int32_t index);
void hosts_disconnect(Hosts* hosts);
// Stop pairing or connecting; the pair / link status goes back to idle.
void hosts_cancel(Hosts* hosts);

// While connected: this phone's pad (pad 0) as a RetroPad, once a frame
// (each packet repeats the last few states: a lost one costs nothing).
struct RetroPad;
void hosts_send_pad(Hosts* hosts, const RetroPad* pad);

// The port this phone's pad plays on at the connected host (1..4), 0 when
// it isn't on one (START joins it) or there's no connection.
int32_t hosts_pad_port(const Hosts* hosts);

// FORGET: unpairs from a host. Online (and nothing else connected): it
// connects, tells the host (which forgets this phone too) and forgets it
// here; else only here. The list changes when it's done.
void hosts_forget(Hosts* hosts, int32_t index);

// The pads' status: offline, connecting, or ready with the last ping.
LinkShown hosts_link_shown(const Hosts* hosts, uint32_t* ping_ms);

// "192.168.1.20" or "192.168.1.20:55420" (the default port when there's none).
bool hosts_parse_address(const char* text, NetAddress* address);

// "WRONG CODE" etc., for the screens.
const char* hosts_refusal_text(NetClientRefusal refusal);
// What PAIR says when pairing failed (hosts_refusal_text, or that the host
// still lists this phone and has to remove it first).
const char* hosts_pair_failure_text(const Hosts* hosts);
