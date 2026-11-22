#pragma once

// The hosts (piccolos) this phone knows and the network to them, on
// frontends-common's frontend_net: this device's identity, the hosts it
// paired with, LAN discovery, pairing and connecting.
//
// Pairing (Andrés, 2026-11-22): the phone shows a one-use 9-digit code
// (net_pair_code_generate) and asks; the piccolo's user types it and picks
// what the phone may do. On the LAN the piccolo is asked directly; across the
// internet the phone waits in the code's pairing room on the beacon while the
// piccolo's user types it in TOOLS > INVITE. A code typed wrong there gives a
// new one here (hosts_pair_code).
// The connection runs on its own thread (net_client_service.h: polled every
// millisecond; remote play's stream decoded there, its audio played there);
// the app's loop drains what it reported (hosts_update), never blocks. Every
// hosts_* function takes the thread's lock while it uses the connection.
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
// up, until the loss passes 2%; any paired level; once per run of the app
// for each host), kept per host (HOME shows it; the stream's quality later).
//
// FORGET unpairs on both sides when the host can be reached (it's told and
// forgets this phone too); offline, only here. A host that unpairs us is
// dropped from the list.
//
// Across the internet (Andrés: the same flow whatever the transport): a host
// that isn't on the LAN is met through its beacon: the one it was paired
// through, or the default one for a host paired on the LAN (Andrés, 2026-11-21: a phone that
// leaves home doesn't pair again). Such a host is tried at its LAN address
// first unless it was last met across the internet; no answer there, then
// the beacon:
// net_client_connect_beacon, UDP hole punching first, then WebRTC through
// the beacon's signaling. In a browser that's the only way (no LAN there:
// no discovery, no ENet; WebRTC only).
//
// Guest links (design-notes "Guest links", Andrés 2026-11-22): a link a
// piccolo made for its running game (https://<beacon>/join/<room>#<token>,
// net_guest.h) lets this phone watch, and play when the host gave a seat and
// a port is free, with no pairing. It's the host at HOSTS_GUEST
// (hosts_set_guest): not in `known`, never saved, met only through the
// beacon; the throwaway key is net_client's.
//
// A discovered host is matched to a paired one by its last address: the
// discovery reply has no key (a fingerprint in it would match after an
// address change; to talk over with the netcode side). A host is "online"
// while it keeps answering discovery.

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_thread.h>

#include "net_beacon.h"
#include "net_client_service.h"
#include "net_content.h"
#include "net_guest.h"
#include "net_host.h"
#include "net_input.h"
#include "net_settings.h"   // NetDevice
#include "net_speed.h"
#include "screens.h"   // LinkShown
#include "settings_file.h"

constexpr int32_t HOSTS_MAX_FOUND = 16;
constexpr int32_t HOSTS_GUEST = -2;   // the guest link's host, as an index (hosts_set_guest)

struct KnownHost
{
    uint8_t        key[NET_KEY_SIZE] = {};
    std::string    name;
    NetAddress     address;                 // last seen on the LAN; port 0 = never
    std::string    beacon;                  // its beacon ("" = the default one), for internet connections
    bool           internet = false;        // paired or last connected across the internet
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

// Outside the browser: a beacon's name to its addresses for UDP hole
// punching (DNS blocks: on its own thread), then what waits for them:
// pairing with a link or connecting to the known host `index`.
struct HostsResolve
{
    SDL_Thread*   thread = nullptr;
    SDL_AtomicInt done = {};
    char          name[256] = {};
    NetAddress    servers[NET_BEACON_MAX_SERVERS];
    uint32_t      count = 0;
    bool          cancelled = false;        // nobody wants it any more: dropped when it ends
    bool          pairing = false;          // else connecting to `index`
    char          code[NET_PAIR_CODE_DIGITS + 1] = {};   // pairing: ours
    int32_t       index = -1;               // connecting: the known host (HOSTS_GUEST: the guest link)
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
    bool                   lan = false;         // LAN discovery and ENet work here (not in a browser)
    std::string            pair_beacon;         // the beacon an internet pairing goes through ("" = the default)
    NetClientService       net;                 // the connection, on its thread (net.client under its lock)
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
    bool                   lan_failed = false;  // this connection's LAN try got no answer: the beacon next
    HostsResolve           resolve;             // the beacon being looked up (not in a browser)
    HostsSpeedTest         speed;
    bool                   has_stats = false;   // `stats` is the ready connection's
    NetLinkStats           stats;
    double                 next_stats = 0.0;    // when to read them again (every 5 s)
    int32_t                pending_unpair = -1; // FORGET: connecting to tell this host, then forget it
    NetRetroPadSender      pad_sender;          // this phone's pad 0, to the connected host
    int8_t                 pad_port = -1;       // the port pad 0 plays on there (controllers), -1 = none
    bool                   controllers_known = false;   // the host has said where our pads play (pad_port is real)
    uint32_t               next_request = 1;    // ids for requests to the host
    bool                   measure_wanted = false;  // run the speed test when this connection is ready (HOME's refresh)
    bool                   measure_only = false;    // connected only for that: leave once it's done
    bool                   game_known = false;  // the host has said whether a game runs (game_state)
    NetGameState           game;
    uint8_t                stream_flags = 0;    // remote play wanted (NET_STREAM_WANT_*): asked for once ready
    NetStreamTuning        tuning;              // how the host should run while we stream (the pacing)
    bool                   tuning_wanted = false;
    NetStreamTuning        tuning_result;       // what the host applies (stream_tuning_result)
    bool                   has_tuning_result = false;
    // GO HEADLESS (Andrés): this phone took the game, piccolo's window is
    // hidden (headless_state). Ends with the stream or the connection.
    bool                   headless_on = false;      // the host runs headless
    bool                   headless_yours = false;   // ... for this phone
    // PAUSE (STREAM access, a game running): the host's "pause" action, from
    // its action list (sent when connected and whenever it changes).
    bool                   pause_offered = false;
    uint32_t               pause_causes = 0;         // NET_PAUSE_* (net_settings.h): USER is the toggle
    // DEVICES (FULL access): who plays on the host's ports. Asked for once
    // (hosts_request_devices); the host sends the list again whenever it changes.
    bool                   devices_wanted = false;
    bool                   devices_known = false;
    std::vector<NetDevice> devices;
    NetClientRefusal       refusal = NetClientRefusal::none;
    bool                   refusal_still_known = false;   // pairing failed because the host still lists this phone
    // Guest links.
    bool                   has_guest = false;   // a guest link is set (hosts_set_guest)
    KnownHost              guest_host;          // its host, as the screens see it (access: STREAM)
    NetGuestLink           guest_link;
    std::string            guest_beacon;        // the link's beacon ("" = the default)
    NetGuestLevel          guest_level = NetGuestLevel::watch;   // what the host gives us now
    uint8_t                guest_port = NET_GUEST_NO_PORT;       // the port our pad plays on there
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

// Asks the host at `address` to pair, with `code` (ours, 9 digits: its user
// types it). Progress in hosts->pair. False if it can't start (another
// connection is running).
bool hosts_pair(Hosts* hosts, NetAddress address, const char* code);
// Across the internet: waits with `code` in its pairing room on `beacon`
// ("" = the default) until a piccolo's user types it in TOOLS > INVITE.
bool hosts_pair_internet(Hosts* hosts, const char* code, const char* beacon);
// The code to show while pairing (a new one after the host typed it wrong).
// False when no pairing runs.
bool hosts_pair_code(Hosts* hosts, char* code);
// The host has our request: its user hasn't typed the code yet (WAITING).
bool hosts_pair_waiting(Hosts* hosts);
// A link that names a host and its beacon (net_pair_link.h; the Android
// intent, the browser's '#'): not used for pairing since 2026-11-22, kept for
// the links to come (a game to join). Its beacon ("" = the default), the one
// PAIR's INTERNET goes through. False if `text` isn't one.
bool hosts_link_beacon(const char* text, std::string* beacon);
// A guest link (any form, net_guest_link_parse): the host at HOSTS_GUEST, to
// connect to with hosts_connect. False if `text` isn't one. hosts_clear_guest
// forgets it (leaving REMOTE PLAY).
bool hosts_set_guest(Hosts* hosts, const char* text);
void hosts_clear_guest(Hosts* hosts);
// The host at `index` (HOSTS_GUEST included), or nullptr.
const KnownHost* hosts_host(const Hosts* hosts, int32_t index);
// Connect to a paired host (entering GAMEPAD...) / leave it. The speed test
// runs once it's ready (once per run for each host).
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

// HOME's refresh: a speed test now. Connected to
// that host already: right away; else it connects, measures and leaves.
// False when it can't (no such host, offline, busy).
bool hosts_measure(Hosts* hosts, int32_t index);
// A speed test for that host is running (or connecting to run one).
bool hosts_measuring(const Hosts* hosts, int32_t index);

// FORGET: unpairs from a host. Online (and nothing else connected): it
// connects, tells the host (which forgets this phone too) and forgets it
// here; else only here. The list changes when it's done.
void hosts_forget(Hosts* hosts, int32_t index);

// Remote play: ask the connected host for its stream (video and audio) from
// now on, also after reconnecting; or stop it. The picture comes from
// hosts_take_frame, the audio plays by itself.
void hosts_stream_start(Hosts* hosts);
void hosts_stream_stop(Hosts* hosts);
// The newest picture, if one came since the last call (net_client_service_take_frame).
bool hosts_take_frame(Hosts* hosts, std::vector<uint8_t>* pixels, uint32_t* width, uint32_t* height,
                      FramePixelFormat* format, uint32_t* frame, NetStreamInfo* info);
// What the host should do while this phone streams (stream_tuning: frame
// delay, run-ahead), sent with the stream and again when it changes. Only in
// the host's memory: its settings aren't touched.
void hosts_set_stream_tuning(Hosts* hosts, const NetStreamTuning* tuning);
// What the host applies now (false until it said).
bool hosts_stream_tuning_result(const Hosts* hosts, NetStreamTuning* tuning);
// How many pictures REMOTE PLAY takes in order (1 = only the newest; net_client_service_set_frame_queue).
void hosts_set_frame_queue(Hosts* hosts, uint32_t depth);
// The stream's counts (frames, losses, bytes, audio).
void hosts_stream_stats(Hosts* hosts, NetClientStreamStats* stats);
// Whether a game runs on the connected host: 1 yes, 0 no, -1 not known yet.
int32_t hosts_game_running(const Hosts* hosts);

// PAUSE: false when the connected host doesn't offer it to this phone (no
// game, below STREAM, not connected). Else why its game is paused
// (NET_PAUSE_* bits, 0 = it runs): USER is the one hosts_toggle_pause sets
// and lifts; the others (a window open there, its focus...) only it lifts.
bool hosts_pause(const Hosts* hosts, uint32_t* causes);
void hosts_toggle_pause(Hosts* hosts);

// DEVICES: ask the connected host who plays on its ports (FULL access; also
// after reconnecting). The list is in hosts->devices once devices_known.
void hosts_request_devices(Hosts* hosts);
// Puts a device of the list on `port` (0..3) only; -1 = on none (it joins
// again with START). The host sends the new list.
void hosts_move_device(Hosts* hosts, const NetDevice* device, int8_t port);

// GO HEADLESS: take the game (the host's window hides; needs STREAM and the
// stream running) or give it back. The host answers with its state
// (headless_on / headless_yours), also when it refused.
void hosts_go_headless(Hosts* hosts, bool on);
// Whether REMOTE PLAY's stream is running now (asked for and connected).
bool hosts_streaming(const Hosts* hosts);

// PAIR's progress line: still connecting (true), or the host has our request.
bool hosts_pair_connecting(Hosts* hosts);

// The pads' status: offline, connecting, or ready with the last ping and loss.
PadLink hosts_pad_link(const Hosts* hosts);

// "192.168.1.20" or "192.168.1.20:55420" (the default port when there's none).
bool hosts_parse_address(const char* text, NetAddress* address);

// "WRONG CODE" etc., for the screens.
const char* hosts_refusal_text(NetClientRefusal refusal);
// What PAIR says when pairing failed (hosts_refusal_text, or that the host
// still lists this phone and has to remove it first).
const char* hosts_pair_failure_text(const Hosts* hosts);
