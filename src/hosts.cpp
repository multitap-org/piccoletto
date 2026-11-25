#include "hosts.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_platform.h>

#include "net_beacon.h"      // NET_BEACON_DEFAULT_SERVER
#include "net_pair_link.h"
#include "net_pairing.h"
#include "net_settings.h"
#include "net_signal.h"
#include "retropad.h"

// Discovery: a broadcast (and a unicast to every paired host's last address,
// for networks that drop broadcasts) this often; a host that stops answering
// is gone after a few missed rounds.
constexpr double DISCOVERY_SECONDS = 2.0;
constexpr double FOUND_SECONDS     = 7.0;
constexpr size_t PAIR_HOST_TEXT    = 64;    // a typed address

// Speed test (net_probe --speedtest's steps): one second per rate, rising
// until the loss passes 2%, down then up. A report that doesn't come within
// a few seconds ends the test (the host may be busy with another).
static const uint32_t SPEED_RATES_KBPS[] = {5000, 10000, 20000, 50000, 100000, 200000};
constexpr uint32_t SPEED_RATE_COUNT = sizeof(SPEED_RATES_KBPS) / sizeof(SPEED_RATES_KBPS[0]);
constexpr uint32_t SPEED_TEST_MS    = 1000;
constexpr double   SPEED_MAX_LOSS   = 0.02;
constexpr double   SPEED_REPORT_WAIT_SECONDS = 4.0;
// The ping is read this often (Andrés: every 5 s, on both sides).
constexpr double   STATS_SECONDS = 5.0;

static std::string key_section(const uint8_t* key)
{
    char hex[2 * NET_KEY_SIZE + 1];
    net_bytes_to_hex(key, NET_KEY_SIZE, hex, sizeof(hex));
    return std::string("host:") + hex;
}

static bool same_address(NetAddress a, NetAddress b)
{
    return net_address_equal(a, b);   // (IPv4 or IPv6)
}

// What hosts.cfg says about a host (address, access level).
static void read_info(const Hosts* hosts, KnownHost* host)
{
    std::string section = key_section(host->key);
    const char* address = settings_file_get(&hosts->info, section.c_str(), "address");
    const char* port = settings_file_get(&hosts->info, section.c_str(), "port");
    if (address != nullptr && port != nullptr)
    {
        long number = std::strtol(port, nullptr, 10);
        NetAddress parsed;
        if (number > 0 && number <= 65535 && net_address_from_text(address, static_cast<uint16_t>(number), &parsed))
        {
            host->address = parsed;
        }
    }
    const char* access = settings_file_get(&hosts->info, section.c_str(), "access");
    if (access != nullptr)
    {
        net_access_level_parse(access, &host->access);
    }
    const char* beacon = settings_file_get(&hosts->info, section.c_str(), "beacon");
    host->beacon = beacon != nullptr ? beacon : "";
    const char* internet = settings_file_get(&hosts->info, section.c_str(), "internet");
    host->internet = internet != nullptr && std::strcmp(internet, "true") == 0;
    const char* down = settings_file_get(&hosts->info, section.c_str(), "down_kbps");
    const char* up = settings_file_get(&hosts->info, section.c_str(), "up_kbps");
    host->down_kbps = down != nullptr ? static_cast<uint32_t>(std::strtoul(down, nullptr, 10)) : 0;
    host->up_kbps = up != nullptr ? static_cast<uint32_t>(std::strtoul(up, nullptr, 10)) : 0;
}

static void write_info(Hosts* hosts, const KnownHost* host)
{
    std::string section = key_section(host->key);
    if (host->address.port != 0)
    {
        char address[32];
        net_address_to_text(host->address, address, sizeof(address));
        // net_address_to_text writes "a.b.c.d:port": keep the two apart.
        char* colon = std::strrchr(address, ':');
        if (colon != nullptr)
        {
            *colon = '\0';
        }
        char port[8];
        std::snprintf(port, sizeof(port), "%u", static_cast<unsigned>(host->address.port));
        settings_file_set(&hosts->info, section.c_str(), "address", address);
        settings_file_set(&hosts->info, section.c_str(), "port", port);
    }
    settings_file_set(&hosts->info, section.c_str(), "access", net_access_level_name(host->access));
    settings_file_set(&hosts->info, section.c_str(), "beacon", host->beacon.c_str());
    settings_file_set(&hosts->info, section.c_str(), "internet", host->internet ? "true" : "false");
    if (host->down_kbps > 0 || host->up_kbps > 0)
    {
        char kbps[16];
        std::snprintf(kbps, sizeof(kbps), "%u", static_cast<unsigned>(host->down_kbps));
        settings_file_set(&hosts->info, section.c_str(), "down_kbps", kbps);
        std::snprintf(kbps, sizeof(kbps), "%u", static_cast<unsigned>(host->up_kbps));
        settings_file_set(&hosts->info, section.c_str(), "up_kbps", kbps);
    }
    settings_file_save(&hosts->info, hosts->info_path.c_str(), "piccoletto's paired hosts: last address and access level");
}

// The known list from the client's trusted hosts (after loading, pairing, forgetting).
static void rebuild_known(Hosts* hosts)
{
    hosts->known.clear();
    for (size_t i = 0; i < hosts->net.client.trust.devices.size(); i++)
    {
        const NetTrustedDevice* device = &hosts->net.client.trust.devices[i];
        KnownHost host;
        std::memcpy(host.key, device->key, NET_KEY_SIZE);
        host.name = device->name;
        read_info(hosts, &host);
        hosts->known.push_back(host);
    }
    for (int32_t i = 0; i < hosts->found_count; i++)
    {
        hosts->found[i].known = -1;
    }
}

bool hosts_create(Hosts* hosts, const char* settings_dir, const char* phone_name)
{
    std::string dir = settings_dir;
    hosts->info_path = dir + "/hosts.cfg";
    settings_file_load(&hosts->info, hosts->info_path.c_str());
    if (!net_init())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Network: can't start; pairing and playing are off");
        return false;
    }
    NetKeypair identity;
    std::string identity_path = dir + "/net_identity.cfg";
    if (!net_identity_load_or_create(identity_path.c_str(), &identity))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Network: no identity at %s; pairing and playing are off",
                     identity_path.c_str());
        net_shutdown();
        return false;
    }
    std::string trust_path = dir + "/net_hosts.cfg";
    bool created = net_client_service_start(&hosts->net, &identity, phone_name, trust_path.c_str());
    net_keypair_wipe(&identity);
    if (!created)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Network: no socket; pairing and playing are off");
        net_client_service_stop(&hosts->net);
        net_shutdown();
        return false;
    }
    // The LAN (discovery, ENet): not in a browser, which has only WebRTC;
    // there every host is met through its beacon.
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    hosts->lan = false;
#else
    hosts->lan = net_host_create_client(&hosts->discovery);
#endif
    if (!hosts->lan)
    {
        SDL_Log("Network: no LAN here: hosts are met across the internet (pair links)");
    }
    net_client_service_lock(&hosts->net);
    rebuild_known(hosts);
    net_client_service_unlock(&hosts->net);
    hosts->ok = true;
    char fingerprint[64];
    net_key_fingerprint(hosts->net.client.our_key, fingerprint, sizeof(fingerprint));
    SDL_Log("Network: this phone is %s, %u paired host(s)", fingerprint, static_cast<unsigned>(hosts->known.size()));
    return true;
}

void hosts_destroy(Hosts* hosts)
{
    if (!hosts->ok)
    {
        return;
    }
    net_client_service_lock(&hosts->net);
    net_client_close(&hosts->net.client);
    net_client_service_unlock(&hosts->net);
    net_client_service_stop(&hosts->net);
    if (hosts->resolve.thread != nullptr)
    {
        SDL_WaitThread(hosts->resolve.thread, nullptr);   // DNS can't be interrupted: at most its timeout
        hosts->resolve.thread = nullptr;
    }
    if (hosts->lan)
    {
        net_host_destroy(&hosts->discovery);
    }
    net_shutdown();
    hosts->ok = false;
}

void hosts_set_name(Hosts* hosts, const char* phone_name)
{
    // As net_client_create keeps it: cut the way the messages carry it.
    uint8_t bytes[NET_DEVICE_NAME_MAX];
    size_t size = net_pair_name_bytes(phone_name, bytes);
    net_client_service_lock(&hosts->net);
    hosts->net.client.name.assign(reinterpret_cast<const char*>(bytes), size);
    net_client_service_unlock(&hosts->net);
}

// A discovery reply: the host's listening address is the sender's IP with
// the port it announces.
static void on_discovered(Hosts* hosts, const NetEvent* event, double now)
{
    NetAddress address = event->address;
    address.port = event->discovered.port;
    FoundHost* found = nullptr;
    for (int32_t i = 0; i < hosts->found_count; i++)
    {
        if (same_address(hosts->found[i].address, address))
        {
            found = &hosts->found[i];
        }
    }
    if (found == nullptr)
    {
        if (hosts->found_count >= HOSTS_MAX_FOUND)
        {
            return;
        }
        found = &hosts->found[hosts->found_count];
        hosts->found_count++;
        *found = FoundHost{};
        found->address = address;
    }
    std::snprintf(found->name, sizeof(found->name), "%s", event->discovered.name);
    found->seen_at = now;
    found->known = -1;
    for (size_t i = 0; i < hosts->known.size(); i++)
    {
        if (same_address(hosts->known[i].address, address))
        {
            found->known = static_cast<int32_t>(i);
        }
    }
}

static void discover(Hosts* hosts, double now)
{
    if (!hosts->lan)
    {
        return;
    }
    if (now >= hosts->next_discovery)
    {
        hosts->next_discovery = now + DISCOVERY_SECONDS;
        net_host_discover(&hosts->discovery, NET_DEFAULT_PORT);
        for (size_t i = 0; i < hosts->known.size(); i++)
        {
            if (hosts->known[i].address.port != 0)
            {
                net_host_discover_at(&hosts->discovery, hosts->known[i].address);
            }
        }
    }
    NetEvent event;
    while (net_host_poll(&hosts->discovery, &event))
    {
        if (event.type == NetEventType::discovered)
        {
            on_discovered(hosts, &event, now);
        }
    }
    // Drop the ones that stopped answering; the rest say who's online.
    int32_t kept = 0;
    for (int32_t i = 0; i < hosts->found_count; i++)
    {
        if (now - hosts->found[i].seen_at <= FOUND_SECONDS)
        {
            hosts->found[kept] = hosts->found[i];
            kept++;
        }
    }
    hosts->found_count = kept;
    for (size_t i = 0; i < hosts->known.size(); i++)
    {
        hosts->known[i].online = false;
    }
    for (int32_t i = 0; i < hosts->found_count; i++)
    {
        int32_t known = hosts->found[i].known;
        if (known >= 0 && known < static_cast<int32_t>(hosts->known.size()))
        {
            hosts->known[static_cast<size_t>(known)].online = true;
        }
    }
}

static void on_ready(Hosts* hosts, const NetClientEvent* event, NetAddress address)
{
    if (event->guest)
    {
        // The link's host proved the token: nothing to save.
        hosts->guest_level = event->guest_level;
        hosts->link = LinkStatus::ready;
        hosts->linked = HOSTS_GUEST;
        SDL_Log("Network: joined %s's game as a guest (%s)", hosts->guest_host.name.c_str(),
                event->guest_level == NetGuestLevel::play ? "watch + play" : "watch");
        return;
    }
    if (event->newly_paired)
    {
        rebuild_known(hosts);
    }
    if (event->device < 0 || event->device >= static_cast<int32_t>(hosts->known.size()))
    {
        return;
    }
    KnownHost* host = &hosts->known[static_cast<size_t>(event->device)];
    bool internet = hosts->net.client.via_beacon || hosts->net.client.via_signal;
    if (!internet)
    {
        host->address = address;
    }
    host->internet = internet;
    if (event->newly_paired && internet)
    {
        host->beacon = hosts->pair_beacon;
    }
    host->access = event->access;
    write_info(hosts, host);
    // Pairing with a host that already knows us (and we it): it welcomes us
    // without asking for the code. Paired all the same.
    if (event->newly_paired || hosts->pair == PairStatus::busy)
    {
        SDL_Log("Network: paired with %s", host->name.c_str());
        hosts->pair = PairStatus::paired;
        hosts->paired_index = event->device;
        // Paired: the modes connect again when they're opened.
        net_client_close(&hosts->net.client);
        return;
    }
    hosts->link = LinkStatus::ready;
    hosts->linked = event->device;
    SDL_Log("Network: connected to %s (%s)", host->name.c_str(), net_access_level_name(host->access));
}

// Drops a host from what hosts.cfg keeps and rebuilds the list (after the
// client's trust list lost it: net_client_unpair, or the host unpaired us).
static void forget_info(Hosts* hosts, const uint8_t* key)
{
    std::string section = key_section(key);
    const char* const KEYS[] = {"address", "port", "access", "down_kbps", "up_kbps"};
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++)
    {
        settings_file_remove(&hosts->info, section.c_str(), KEYS[i]);
    }
    settings_file_save(&hosts->info, hosts->info_path.c_str(), "piccoletto's paired hosts: last address and access level");
}

static void forget_here(Hosts* hosts, int32_t index)
{
    uint8_t key[NET_KEY_SIZE];
    std::memcpy(key, hosts->known[static_cast<size_t>(index)].key, NET_KEY_SIZE);
    net_client_unpair(&hosts->net.client, static_cast<uint32_t>(index));   // not connected: only our list
    forget_info(hosts, key);
    rebuild_known(hosts);
}

// --- speed test ----------------------------------------------------------------

static void speed_finish(Hosts* hosts)
{
    HostsSpeedTest* speed = &hosts->speed;
    speed->running = false;
    speed->waiting = false;
    speed->receiver.active = false;
    speed->sender.active = false;
    if (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
    {
        const KnownHost* host = &hosts->known[static_cast<size_t>(hosts->linked)];
        SDL_Log("Network: speed to %s: %.1f Mbit/s down, %.1f up", host->name.c_str(), host->down_kbps / 1000.0,
                host->up_kbps / 1000.0);
        hosts->known[static_cast<size_t>(hosts->linked)].measured = true;
        write_info(hosts, host);
    }
    if (hosts->measure_only)
    {
        // Connected only to measure (HOME's refresh): done, leave.
        hosts->measure_only = false;
        hosts_disconnect(hosts);
    }
}

// The next rate, or the next direction, or done.
static void speed_next(Hosts* hosts)
{
    HostsSpeedTest* speed = &hosts->speed;
    if (speed->step >= SPEED_RATE_COUNT)
    {
        if (speed->direction == NetSpeedDirection::up)
        {
            speed_finish(hosts);
            return;
        }
        speed->direction = NetSpeedDirection::up;
        speed->step = 0;
    }
    uint32_t id = speed->next_id++;
    uint32_t rate = SPEED_RATES_KBPS[speed->step];
    uint8_t out[32];
    size_t size = net_speed_write_request(out, sizeof(out), id, speed->direction, rate, SPEED_TEST_MS);
    if (!net_client_send(&hosts->net.client, NetChannel::control, out, size))
    {
        speed_finish(hosts);
        return;
    }
    if (speed->direction == NetSpeedDirection::down)
    {
        net_speed_receiver_start(&speed->receiver, id, 0, 0.0);
    }
    else
    {
        net_speed_sender_start(&speed->sender, id, 0, rate, SPEED_TEST_MS, net_seconds());
    }
    speed->waiting = true;
    speed->asked_at = net_seconds();
}

static void speed_start(Hosts* hosts)
{
    hosts->speed = HostsSpeedTest{};
    hosts->speed.running = true;
    if (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
    {
        hosts->known[static_cast<size_t>(hosts->linked)].down_kbps = 0;
        hosts->known[static_cast<size_t>(hosts->linked)].up_kbps = 0;
    }
    speed_next(hosts);
}

// A report: what got through at this rate; the next rate, or the next
// direction once the loss is too high.
static void speed_report(Hosts* hosts, const NetSpeedReport* report)
{
    HostsSpeedTest* speed = &hosts->speed;
    if (!speed->running || !speed->waiting || report->direction != speed->direction)
    {
        return;
    }
    double sent = 0.0;
    double received = 0.0;
    double bytes = 0.0;
    double seconds = 0.0;
    if (report->direction == NetSpeedDirection::down)
    {
        if (report->id != speed->receiver.id)
        {
            return;
        }
        NetSpeedReport ours;
        net_speed_receiver_report(&speed->receiver, NetSpeedDirection::down, &ours);
        sent = report->packets;
        received = ours.packets;
        bytes = static_cast<double>(ours.bytes);
        seconds = report->seconds;
        speed->receiver.active = false;
    }
    else
    {
        if (report->id != speed->sender.id)
        {
            return;
        }
        sent = speed->sender.sent;
        received = report->packets;
        bytes = static_cast<double>(report->bytes);
        seconds = speed->sender.duration;
    }
    double loss = sent > 0.0 ? 1.0 - received / sent : 1.0;
    loss = loss < 0.0 ? 0.0 : loss;
    uint32_t kbps = seconds > 0.0 ? static_cast<uint32_t>(bytes * 8.0 / seconds / 1000.0) : 0;
    SDL_Log("Network: speed %s asked %.0f Mbit/s, got %.1f, loss %.1f%% (%u of %u packets)",
            report->direction == NetSpeedDirection::down ? "down" : "up", SPEED_RATES_KBPS[speed->step] / 1000.0,
            kbps / 1000.0, loss * 100.0, static_cast<unsigned>(received), static_cast<unsigned>(sent));
    if (loss <= SPEED_MAX_LOSS && hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
    {
        KnownHost* host = &hosts->known[static_cast<size_t>(hosts->linked)];
        uint32_t* best = report->direction == NetSpeedDirection::down ? &host->down_kbps : &host->up_kbps;
        *best = kbps > *best ? kbps : *best;
    }
    speed->waiting = false;
    speed->step = loss > SPEED_MAX_LOSS ? SPEED_RATE_COUNT : speed->step + 1;
    speed_next(hosts);
}

// Once a frame while a test runs: our packets going up, a report that never comes.
static void speed_update(Hosts* hosts)
{
    HostsSpeedTest* speed = &hosts->speed;
    if (!speed->running)
    {
        return;
    }
    if (speed->sender.active)
    {
        uint8_t packet[NET_SPEED_PACKET_SIZE];
        uint32_t due = net_speed_sender_due(&speed->sender, net_seconds());
        for (uint32_t i = 0; i < due; i++)
        {
            size_t size = net_speed_write_data(packet, sizeof(packet), speed->sender.id, speed->sender.sent);
            net_client_send(&hosts->net.client, NetChannel::input, packet, size);
            net_speed_sender_sent(&speed->sender, size);
        }
    }
    if (speed->waiting && net_seconds() - speed->asked_at > SPEED_REPORT_WAIT_SECONDS)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Network: no speed report from the host; test stopped");
        speed_finish(hosts);
    }
}

// A message from the host (control or input).
static void on_message(Hosts* hosts, const NetClientEvent* event)
{
    if (event->channel == NetChannel::input)
    {
        net_speed_receive(&hosts->speed.receiver, event->data, event->size, net_seconds());
        return;
    }
    NetSpeedReport report;
    if (event->size > 0 && event->data[0] == NET_SPEED_REPORT && net_speed_read_report(event->data, event->size, &report))
    {
        speed_report(hosts, &report);
        return;
    }
    // What the host applies of our stream_tuning (again whenever it changes).
    if (event->size > 0 && event->data[0] == NET_STREAM_TUNING_RESULT)
    {
        NetStreamTuning result;
        if (net_stream_read_tuning(event->data, event->size, NET_STREAM_TUNING_RESULT, &result))
        {
            if (!hosts->has_tuning_result || result.delay_ms != hosts->tuning_result.delay_ms ||
                result.delay != hosts->tuning_result.delay)
            {
                SDL_Log("Network: the host runs the stream with a %u ms frame delay (mode %d), run-ahead %u",
                        static_cast<unsigned>(result.delay_ms), static_cast<int>(result.delay),
                        static_cast<unsigned>(result.run_ahead));
            }
            hosts->tuning_result = result;
            hosts->has_tuning_result = true;
        }
        return;
    }
    // GO HEADLESS: the host's state (the answer to ours, or a change on its side).
    if (event->size > 0 && event->data[0] == NET_STREAM_HEADLESS_STATE)
    {
        bool on = false;
        bool yours = false;
        if (net_stream_read_headless_state(event->data, event->size, &on, &yours))
        {
            if (on != hosts->headless_on || yours != hosts->headless_yours)
            {
                SDL_Log("Network: the host %s", on ? (yours ? "is headless: this phone has the game" : "is headless for another device")
                                                   : "shows its window again");
            }
            hosts->headless_on = on;
            hosts->headless_yours = yours;
        }
        return;
    }
    // DEVICES: who plays on the host's ports (asked for, then sent whenever it changes).
    if (event->size > 0 && event->data[0] == static_cast<uint8_t>(NetSettingsMessage::devices))
    {
        uint32_t id = 0;
        if (net_settings_read_devices(event->data, event->size, &id, &hosts->devices))
        {
            hosts->devices_known = true;
        }
        return;
    }
    // The actions the host offers us (sent when connected, again whenever
    // the list or a state in it changes): only PAUSE is used so far.
    if (event->size > 0 && event->data[0] == static_cast<uint8_t>(NetSettingsMessage::actions_result))
    {
        uint32_t id = 0;
        std::vector<NetAction> actions;
        if (net_settings_read_actions(event->data, event->size, &id, &actions))
        {
            bool offered = false;
            uint32_t causes = 0;
            for (size_t i = 0; i < actions.size(); i++)
            {
                if (actions[i].key == "pause" && actions[i].kind == NetActionKind::toggle)
                {
                    offered = true;
                    causes = actions[i].state;
                }
            }
            if (offered && causes != hosts->pause_causes)
            {
                SDL_Log("Network: the host's game %s (causes 0x%x)", causes != 0 ? "is paused" : "runs", causes);
            }
            hosts->pause_offered = offered;
            hosts->pause_causes = offered ? causes : 0;
        }
        return;
    }
    // A guest's port at the host (or none: it watches).
    uint8_t guest_port = NET_GUEST_NO_PORT;
    if (event->size > 0 && net_guest_read_port(event->data, event->size, &guest_port))
    {
        if (guest_port != hosts->guest_port)
        {
            SDL_Log("Network: as a guest, %s", guest_port == NET_GUEST_NO_PORT ? "no port (watching)" : "on a port");
        }
        hosts->guest_port = guest_port;
        hosts->pad_port = guest_port == NET_GUEST_NO_PORT ? static_cast<int8_t>(-1) : static_cast<int8_t>(guest_port);
        hosts->controllers_known = true;
        return;
    }
    // Whether a game runs (sent when connected and when it changes).
    if (event->size > 0 && event->data[0] == NET_GAME_STATE_MESSAGE)
    {
        if (net_content_read_game_state(event->data, event->size, &hosts->game))
        {
            hosts->game_known = true;
            if (hosts->linked == HOSTS_GUEST && !hosts->game.game.empty())
            {
                // A guest knows its host by the game (no name was paired).
                char title[NET_GUEST_MAX_TITLE + 1];
                net_guest_title_clean(hosts->game.game.c_str(), title);
                hosts->guest_host.name = title;
            }
            SDL_Log("Network: the host %s", hosts->game.running ? "runs a game" : "runs no game");
        }
        return;
    }
    // Where our pads play (asked when connected, pushed when it changes).
    if (event->size > 0 && event->data[0] == static_cast<uint8_t>(NetSettingsMessage::controllers))
    {
        uint32_t id = 0;
        NetControllers controllers;
        if (net_settings_read_controllers(event->data, event->size, &id, &controllers))
        {
            if (controllers.pad_ports[0] != hosts->pad_port)
            {
                SDL_Log("Network: this phone's pad is on port %d", controllers.pad_ports[0] + 1);
            }
            hosts->pad_port = controllers.pad_ports[0];
            hosts->controllers_known = true;
        }
    }
}

// The host's devices list (under the lock): devices_request with FULL access
// (DEVICES moves them), devices_watch below it (the ports' tabs, read only).
static void send_devices_request(Hosts* hosts)
{
    if (!hosts->devices_wanted || hosts->link != LinkStatus::ready || hosts->linked < 0 ||
        hosts->linked >= static_cast<int32_t>(hosts->known.size()))
    {
        return;   // a guest has no DEVICES
    }
    bool full = net_access_allows(hosts->known[static_cast<size_t>(hosts->linked)].access, NetAccessLevel::full);
    uint8_t message[16];
    size_t size = full ? net_settings_write_devices_request(message, sizeof(message), hosts->next_request++)
                       : net_settings_write_devices_watch(message, sizeof(message), hosts->next_request++);
    if (size > 0)
    {
        net_client_send(&hosts->net.client, NetChannel::control, message, size);
    }
}

// stream_tuning to the host, while streaming (under the lock).
static void send_tuning(Hosts* hosts)
{
    if (!hosts->tuning_wanted || hosts->stream_flags == 0 || hosts->link != LinkStatus::ready)
    {
        return;
    }
    uint8_t message[8];
    size_t size = net_stream_write_tuning(message, sizeof(message), NET_STREAM_TUNING, &hosts->tuning);
    if (size > 0)
    {
        net_client_send(&hosts->net.client, NetChannel::control, message, size);
    }
}

// The beacon's signaling for WebRTC (net_signal.h); "" = the default beacon.
static void use_beacon(Hosts* hosts, const std::string& beacon)
{
    const char* name = beacon.empty() ? NET_BEACON_DEFAULT_SERVER : beacon.c_str();
    std::string url = net_signal_url(name);
    std::string stun = net_signal_stun(name);
    net_client_set_signal(&hosts->net.client, url.c_str(), stun.c_str());
}

// --- the beacon's addresses (UDP hole punching; not in a browser) -------------------

static int SDLCALL resolve_thread(void* data)
{
    HostsResolve* resolve = static_cast<HostsResolve*>(data);
    resolve->count = net_address_resolve(resolve->name, NET_BEACON_DEFAULT_PORT, resolve->servers);
    SDL_SetAtomicInt(&resolve->done, 1);
    return 0;
}

// The connection is in use: running, or a beacon lookup hasn't ended (even
// one nobody wants any more: the next connection waits for it).
static bool client_busy(const Hosts* hosts)
{
    return hosts->net.client.state != NetClientState::idle || hosts->resolve.thread != nullptr;
}

// Starts looking up `beacon` ("" = the default one); resolve_done goes on.
// False if the last lookup hasn't ended (even if nobody wants it any more).
static bool resolve_start(Hosts* hosts, const std::string& beacon)
{
    HostsResolve* resolve = &hosts->resolve;
    if (resolve->thread != nullptr)
    {
        return false;
    }
    const char* name = beacon.empty() ? NET_BEACON_DEFAULT_SERVER : beacon.c_str();
    std::snprintf(resolve->name, sizeof(resolve->name), "%s", name);
    resolve->count = 0;
    resolve->cancelled = false;
    SDL_SetAtomicInt(&resolve->done, 0);
    resolve->thread = SDL_CreateThread(resolve_thread, "piccoletto beacon lookup", resolve);
    return resolve->thread != nullptr;
}

// The lookup ended: pair or connect through the beacon if still wanted (UDP
// with the addresses found, else WebRTC through its signaling).
static void resolve_done(Hosts* hosts)
{
    HostsResolve* resolve = &hosts->resolve;
    if (resolve->thread == nullptr || SDL_GetAtomicInt(&resolve->done) == 0)
    {
        return;
    }
    SDL_WaitThread(resolve->thread, nullptr);
    resolve->thread = nullptr;
    if (resolve->cancelled)
    {
        return;
    }
    NetClient* client = &hosts->net.client;
    bool started = false;
    if (resolve->pairing)
    {
        started = net_client_pair_beacon(client, resolve->servers, resolve->count, resolve->code);
    }
    else if (resolve->index == HOSTS_GUEST)
    {
        started = hosts->has_guest &&
                  net_client_join_guest(client, &hosts->guest_link, resolve->servers, resolve->count);
    }
    else
    {
        started = net_client_connect_beacon(client, resolve->servers, resolve->count,
                                            static_cast<uint32_t>(resolve->index));
    }
    SDL_Log("Network: %s through %s (%u address(es))%s", resolve->pairing ? "pairing" : "connecting", resolve->name,
            static_cast<unsigned>(resolve->count), started ? "" : ": couldn't start");
    if (started)
    {
        return;
    }
    hosts->refusal = NetClientRefusal::beacon_unreachable;
    if (resolve->pairing && hosts->pair == PairStatus::busy)
    {
        hosts->pair = PairStatus::failed;
    }
    if (!resolve->pairing && hosts->link == LinkStatus::connecting)
    {
        hosts->link = LinkStatus::failed;
        hosts->measure_wanted = false;
        hosts->measure_only = false;
    }
}

// Connects to the known host `index` as an already paired device: on the
// LAN when it's there (answering discovery, or never met across the
// internet, unless that try just got no answer), else through its beacon
// (the default one for hosts paired on the LAN): its addresses looked up first (UDP
// hole punching, then WebRTC); in a browser WebRTC through the signaling.
static bool start_connect(Hosts* hosts, int32_t index)
{
    hosts->guest_ended = false;
    if (index == HOSTS_GUEST)
    {
        // A guest link: only through its beacon (UDP, then WebRTC).
        if (!hosts->has_guest)
        {
            return false;
        }
        hosts->connecting_to = NetAddress{};
        use_beacon(hosts, hosts->guest_beacon);
        if (hosts->lan)
        {
            if (!resolve_start(hosts, hosts->guest_beacon))
            {
                return false;
            }
            hosts->resolve.pairing = false;
            hosts->resolve.index = HOSTS_GUEST;
            return true;
        }
        return net_client_join_guest(&hosts->net.client, &hosts->guest_link, nullptr, 0);
    }
    const KnownHost* host = &hosts->known[static_cast<size_t>(index)];
    if (hosts->lan && host->address.port != 0 && (host->online || !host->internet) && !hosts->lan_failed)
    {
        hosts->connecting_to = host->address;
        return net_client_connect(&hosts->net.client, host->address, nullptr);
    }
    hosts->connecting_to = NetAddress{};
    use_beacon(hosts, host->beacon);
    if (hosts->lan)
    {
        if (!resolve_start(hosts, host->beacon))
        {
            return false;
        }
        hosts->resolve.pairing = false;
        hosts->resolve.index = index;
        return true;
    }
    return net_client_connect_beacon(&hosts->net.client, nullptr, 0, static_cast<uint32_t>(index));
}

static void update_locked(Hosts* hosts, double now)
{
    discover(hosts, now);
    resolve_done(hosts);
    NetClientEvent event;
    while (net_client_service_next(&hosts->net, &event))
    {
        switch (event.type)
        {
        case NetClientEventType::none:
            break;
        case NetClientEventType::message:   // speed test; game state, controllers: later
            on_message(hosts, &event);
            break;
        case NetClientEventType::ready:
            if (hosts->pending_unpair >= 0)
            {
                // FORGET reached the host: tell it, then it's gone here too.
                int32_t index = hosts->pending_unpair;
                hosts->pending_unpair = -1;
                if (index < static_cast<int32_t>(hosts->known.size()))
                {
                    uint8_t key[NET_KEY_SIZE];
                    std::memcpy(key, hosts->known[static_cast<size_t>(index)].key, NET_KEY_SIZE);
                    net_client_unpair(&hosts->net.client, static_cast<uint32_t>(index));
                    forget_info(hosts, key);
                    rebuild_known(hosts);
                    SDL_Log("Network: unpaired on both sides");
                }
                break;
            }
            on_ready(hosts, &event, hosts->connecting_to);
            if (hosts->link == LinkStatus::ready)
            {
                hosts->next_stats = 0.0;   // the first ping right away
                hosts->pad_sender = NetRetroPadSender{};
                hosts->pad_port = -1;
                hosts->controllers_known = false;
                hosts->game_known = false;
                hosts->devices_known = false;
                hosts->devices.clear();
                hosts->headless_on = false;
                hosts->headless_yours = false;
                hosts->pause_offered = false;
                send_devices_request(hosts);
                if (hosts->stream_flags != 0)
                {
                    bool asked = net_client_service_set_stream(&hosts->net, hosts->stream_flags);
                    SDL_Log("Network: remote play %s", asked ? "asked for" : "NOT asked for (not ready)");
                    hosts->has_tuning_result = false;
                    send_tuning(hosts);
                }
                if (hosts->linked == HOSTS_GUEST)
                {
                    // A guest: its port comes by itself; no speed test, no controllers.
                    hosts->guest_port = NET_GUEST_NO_PORT;
                    hosts->measure_wanted = false;
                    break;
                }
                uint8_t request[16];
                size_t size = net_settings_write_controllers_request(request, sizeof(request), hosts->next_request++);
                net_client_send(&hosts->net.client, NetChannel::control, request, size);
                const KnownHost* host = &hosts->known[static_cast<size_t>(hosts->linked)];
                // Any paired level may run it (Andrés, 2026-11-20).
                if (!host->measured || hosts->measure_wanted)
                {
                    speed_start(hosts);
                }
                hosts->measure_wanted = false;
            }
            break;
        case NetClientEventType::unpaired:
        {
            // The host unpaired us: net_client dropped it from our list.
            SDL_Log("Network: the host unpaired this phone");
            if (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
            {
                forget_info(hosts, hosts->known[static_cast<size_t>(hosts->linked)].key);
            }
            rebuild_known(hosts);
            hosts->link = LinkStatus::failed;
            hosts->linked = -1;
            hosts->refusal = NetClientRefusal::not_paired;
            break;
        }
        case NetClientEventType::access:
            if (event.guest)
            {
                hosts->guest_level = event.guest_level;
                SDL_Log("Network: the host set this guest to %s",
                        event.guest_level == NetGuestLevel::play ? "watch + play" : "watch");
                break;
            }
            if (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
            {
                KnownHost* host = &hosts->known[static_cast<size_t>(hosts->linked)];
                // Told live (and in the welcome when it changed offline). The
                // UI reads host->access each frame; the host already stopped
                // what the new level doesn't allow (the stream, headless).
                SDL_Log("Network: %s set this phone to %s", host->name.c_str(), net_access_level_name(event.access));
                host->access = event.access;
                write_info(hosts, host);
                // Raised to FULL with DEVICES wanted: ask now; lowered: the list goes.
                if (net_access_allows(event.access, NetAccessLevel::full))
                {
                    send_devices_request(hosts);
                }
                else
                {
                    hosts->devices_known = false;
                    hosts->devices.clear();
                }
            }
            break;
        case NetClientEventType::refused:
            SDL_Log("Network: refused (%s)", hosts_refusal_text(event.refusal));
            if (hosts->pending_unpair >= 0)
            {
                // FORGET couldn't tell the host: forget it here anyway.
                int32_t index = hosts->pending_unpair;
                hosts->pending_unpair = -1;
                if (index < static_cast<int32_t>(hosts->known.size()))
                {
                    forget_here(hosts, index);
                }
                break;
            }
            hosts->refusal = event.refusal;
            if (hosts->pair == PairStatus::busy)
            {
                hosts->pair = PairStatus::failed;
                // Welcomed by a host we don't know while pairing: it still
                // lists this phone (forgotten here, not there), so it didn't
                // ask for the code, and its key can't be trusted without it.
                if (event.refusal == NetClientRefusal::unknown_host)
                {
                    hosts->refusal_still_known = true;
                }
            }
            // A host paired on the LAN that didn't answer at its LAN address
            // (the phone left home): through the default beacon instead,
            // once this connection is gone (Andrés, 2026-11-21).
            if (hosts->link == LinkStatus::connecting && event.refusal == NetClientRefusal::connection &&
                hosts->connecting_to.port != 0 && !hosts->lan_failed && hosts->linked >= 0 &&
                hosts->linked < static_cast<int32_t>(hosts->known.size()) &&
                !hosts->known[static_cast<size_t>(hosts->linked)].online)
            {
                SDL_Log("Network: no answer on the LAN; through the beacon");
                hosts->lan_failed = true;
                hosts->connect_pending = true;
                hosts->refusal = NetClientRefusal::none;
                break;
            }
            if (hosts->link == LinkStatus::connecting)
            {
                hosts->link = LinkStatus::failed;
                hosts->measure_wanted = false;
                hosts->measure_only = false;
            }
            break;
        case NetClientEventType::code_changed:
            // The host's user typed our code wrong: PAIR shows the new one
            // (hosts_pair_code) and the host's dialog asks again.
            SDL_Log("Network: the code was typed wrong there: a new one");
            break;
        case NetClientEventType::closed:
            hosts->speed.running = false;
            hosts->game_known = false;
            hosts->pause_offered = false;
            hosts->devices_known = false;
            hosts->devices.clear();
            hosts->headless_on = false;
            hosts->headless_yours = false;
            if (hosts->link == LinkStatus::ready)
            {
                hosts->link = LinkStatus::failed;
                hosts->refusal = NetClientRefusal::connection;
                // A guest the host said goodbye to: why.
                if (event.guest && event.guest_bye == NetGuestBye::kicked)
                {
                    hosts->refusal = NetClientRefusal::guest_kicked;
                }
                else if (event.guest && event.guest_bye == NetGuestBye::ended)
                {
                    hosts->refusal = NetClientRefusal::guest_link;
                    hosts->guest_ended = true;
                }
            }
            break;
        }
    }
    if (hosts->connect_pending && !client_busy(hosts))
    {
        hosts->connect_pending = false;
        bool valid = hosts->linked == HOSTS_GUEST ||
                     (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()));
        if (!valid || !start_connect(hosts, hosts->linked))
        {
            hosts->link = LinkStatus::failed;
            hosts->refusal = NetClientRefusal::connection;
        }
    }
    if (hosts->link != LinkStatus::ready)
    {
        hosts->has_stats = false;
    }
    else if (now >= hosts->next_stats)
    {
        hosts->next_stats = now + STATS_SECONDS;
        hosts->has_stats = net_client_stats(&hosts->net.client, &hosts->stats);
    }
    speed_update(hosts);
}

void hosts_update(Hosts* hosts, double now)
{
    if (!hosts->ok)
    {
        return;
    }
    net_client_service_lock(&hosts->net);
    update_locked(hosts, now);
    net_client_service_unlock(&hosts->net);
}

int32_t hosts_unpaired_count(const Hosts* hosts)
{
    int32_t count = 0;
    for (int32_t i = 0; i < hosts->found_count; i++)
    {
        count += hosts->found[i].known < 0 ? 1 : 0;
    }
    return count;
}

const FoundHost* hosts_unpaired(const Hosts* hosts, int32_t index)
{
    for (int32_t i = 0; i < hosts->found_count; i++)
    {
        if (hosts->found[i].known >= 0)
        {
            continue;
        }
        if (index == 0)
        {
            return &hosts->found[i];
        }
        index--;
    }
    return nullptr;
}

// A pairing started: PAIR follows it in hosts->pair. The phone asks as a
// piccoletto (the host shows it) for GAMEPAD; the host's user decides
// (Andrés, 2026-11-22: net_client_set_pairing before each start).
static void start_pairing(Hosts* hosts)
{
    hosts->pair = PairStatus::busy;
    hosts->paired_index = -1;
    hosts->refusal = NetClientRefusal::none;
    hosts->refusal_still_known = false;
}

static bool pair_internet_locked(Hosts* hosts, const char* code, const char* beacon)
{
    if (!hosts->ok || client_busy(hosts) || code == nullptr || !net_pair_code_valid(code))
    {
        return false;
    }
    std::string name = beacon != nullptr ? beacon : "";
    use_beacon(hosts, name);
    net_client_set_pairing(&hosts->net.client, NetAccessLevel::gamepad, NetPairKind::piccoletto);
    if (hosts->lan)
    {
        // Its addresses first (hosts_update pairs once they're found).
        if (!resolve_start(hosts, name))
        {
            return false;
        }
        hosts->resolve.pairing = true;
        std::snprintf(hosts->resolve.code, sizeof(hosts->resolve.code), "%s", code);
    }
    else if (!net_client_pair_beacon(&hosts->net.client, nullptr, 0, code))
    {
        return false;
    }
    start_pairing(hosts);
    hosts->connecting_to = NetAddress{};
    hosts->pair_beacon = name;
    return true;
}

bool hosts_link_beacon(const char* text, std::string* beacon)
{
    uint8_t key[NET_KEY_SIZE];
    return text != nullptr && std::strchr(text, '#') != nullptr && net_pair_link_parse(text, key, beacon);
}

static bool pair_locked(Hosts* hosts, NetAddress address, const char* code)
{
    if (!hosts->ok || client_busy(hosts) || code == nullptr || !net_pair_code_valid(code))
    {
        return false;
    }
    net_client_set_pairing(&hosts->net.client, NetAccessLevel::gamepad, NetPairKind::piccoletto);
    if (!net_client_connect(&hosts->net.client, address, code))
    {
        return false;
    }
    start_pairing(hosts);
    hosts->connecting_to = address;
    hosts->pair_beacon.clear();
    return true;
}

static bool connect_locked(Hosts* hosts, int32_t index)
{
    hosts->measure_only = false;   // a mode wants the connection: it stays
    bool guest = index == HOSTS_GUEST && hosts->has_guest;
    if (!hosts->ok || (!guest && (index < 0 || index >= static_cast<int32_t>(hosts->known.size()))))
    {
        return false;
    }
    if (hosts->link == LinkStatus::ready && hosts->linked == index)
    {
        return true;   // already connected (GAMEPAD to REMOTE PLAY)
    }
    if (hosts->pending_unpair >= 0)
    {
        return false;   // FORGET is using the connection
    }
    hosts_disconnect(hosts);
    hosts->link = LinkStatus::connecting;
    hosts->linked = index;
    hosts->refusal = NetClientRefusal::none;
    hosts->lan_failed = false;
    // The last connection may still be closing (HOME, then a mode right
    // away): then hosts_update connects once it's gone.
    hosts->connect_pending = client_busy(hosts);
    if (!hosts->connect_pending && !start_connect(hosts, index))
    {
        hosts->link = LinkStatus::failed;
        hosts->refusal = NetClientRefusal::connection;
        return false;
    }
    return true;
}

void hosts_disconnect(Hosts* hosts)
{
    if (hosts->ok && hosts->pending_unpair < 0)
    {
        net_client_service_lock(&hosts->net);
        net_client_service_set_stream(&hosts->net, 0);   // stream_stop first; the picture and audio go
        net_client_close(&hosts->net.client);
        hosts->resolve.cancelled = true;   // a lookup still running is dropped when it ends
        net_client_service_unlock(&hosts->net);
    }
    hosts->speed.running = false;
    hosts->has_stats = false;
    hosts->connect_pending = false;
    hosts->pause_offered = false;
    hosts->devices_wanted = false;   // the next mode asks again if it shows DEVICES
    hosts->devices_known = false;
    hosts->devices.clear();
    hosts->measure_wanted = false;
    hosts->measure_only = false;
    hosts->link = LinkStatus::idle;
    hosts->linked = -1;
}

void hosts_cancel(Hosts* hosts)
{
    hosts_disconnect(hosts);
    hosts->pair = PairStatus::idle;
}

static void forget_locked(Hosts* hosts, int32_t index)
{
    if (!hosts->ok || index < 0 || index >= static_cast<int32_t>(hosts->known.size()))
    {
        return;
    }
    // Connected to it now: tell it right away.
    if (hosts->link == LinkStatus::ready && hosts->linked == index)
    {
        uint8_t key[NET_KEY_SIZE];
        std::memcpy(key, hosts->known[static_cast<size_t>(index)].key, NET_KEY_SIZE);
        net_client_unpair(&hosts->net.client, static_cast<uint32_t>(index));
        hosts->link = LinkStatus::idle;
        hosts->linked = -1;
        forget_info(hosts, key);
        rebuild_known(hosts);
        return;
    }
    // Reachable: connect to tell it (hosts_update finishes it); else only here.
    const KnownHost* host = &hosts->known[static_cast<size_t>(index)];
    if (host->online && host->address.port != 0 && hosts->net.client.state == NetClientState::idle &&
        net_client_connect(&hosts->net.client, host->address, nullptr))
    {
        hosts->pending_unpair = index;
        hosts->connecting_to = host->address;
        return;
    }
    forget_here(hosts, index);
}

static bool measure_locked(Hosts* hosts, int32_t index)
{
    if (!hosts->ok || index < 0 || index >= static_cast<int32_t>(hosts->known.size()))
    {
        return false;
    }
    if (hosts->link == LinkStatus::ready && hosts->linked == index)
    {
        if (!hosts->speed.running)
        {
            speed_start(hosts);
        }
        return true;
    }
    if (hosts->link == LinkStatus::connecting || hosts->link == LinkStatus::ready ||
        hosts->pending_unpair >= 0)
    {
        return false;
    }
    if (!connect_locked(hosts, index))
    {
        return false;
    }
    hosts->measure_wanted = true;
    hosts->measure_only = true;
    return true;
}

bool hosts_measuring(const Hosts* hosts, int32_t index)
{
    if (hosts->linked != index)
    {
        return false;
    }
    return hosts->speed.running || (hosts->link == LinkStatus::connecting && hosts->measure_wanted);
}

static void send_pad_locked(Hosts* hosts, const RetroPad* pad)
{
    if (!hosts->ok || hosts->link != LinkStatus::ready ||
        (hosts->linked == HOSTS_GUEST && hosts->guest_level != NetGuestLevel::play))
    {
        return;   // a guest that only watches has no pad there
    }
    NetRetroPadState state;
    state.buttons = pad->buttons;
    for (int32_t stick = 0; stick < 2; stick++)
    {
        state.analog[stick][0] = pad->analog[stick][0];
        state.analog[stick][1] = pad->analog[stick][1];
    }
    uint8_t packet[NET_INPUT_MAX_PACKET];
    size_t size = net_input_push_retropad(&hosts->pad_sender, 0, &state, packet, sizeof(packet));
    if (size > 0)
    {
        net_client_send(&hosts->net.client, NetChannel::input, packet, size);
    }
}

// The public ones: the connection's lock around the work.

bool hosts_pair(Hosts* hosts, NetAddress address, const char* code)
{
    net_client_service_lock(&hosts->net);
    bool started = pair_locked(hosts, address, code);
    net_client_service_unlock(&hosts->net);
    return started;
}

bool hosts_pair_internet(Hosts* hosts, const char* code, const char* beacon)
{
    net_client_service_lock(&hosts->net);
    bool started = pair_internet_locked(hosts, code, beacon);
    net_client_service_unlock(&hosts->net);
    return started;
}

bool hosts_pair_code(Hosts* hosts, char* code)
{
    net_client_service_lock(&hosts->net);
    bool has = hosts->pair == PairStatus::busy && hosts->net.client.has_code;
    if (has)
    {
        std::memcpy(code, hosts->net.client.code, NET_PAIR_CODE_DIGITS + 1);
    }
    net_client_service_unlock(&hosts->net);
    return has;
}

bool hosts_pair_waiting(Hosts* hosts)
{
    net_client_service_lock(&hosts->net);
    NetClientState state = hosts->net.client.state;
    bool waiting = hosts->pair == PairStatus::busy &&
                   (state == NetClientState::pairing || state == NetClientState::confirming);
    net_client_service_unlock(&hosts->net);
    return waiting;
}

bool hosts_connect(Hosts* hosts, int32_t index)
{
    net_client_service_lock(&hosts->net);
    bool started = connect_locked(hosts, index);
    net_client_service_unlock(&hosts->net);
    return started;
}

void hosts_forget(Hosts* hosts, int32_t index)
{
    net_client_service_lock(&hosts->net);
    forget_locked(hosts, index);
    net_client_service_unlock(&hosts->net);
}

bool hosts_measure(Hosts* hosts, int32_t index)
{
    net_client_service_lock(&hosts->net);
    bool started = measure_locked(hosts, index);
    net_client_service_unlock(&hosts->net);
    return started;
}

void hosts_send_pad(Hosts* hosts, const RetroPad* pad)
{
    net_client_service_lock(&hosts->net);
    send_pad_locked(hosts, pad);
    net_client_service_unlock(&hosts->net);
}

bool hosts_pair_connecting(Hosts* hosts)
{
    net_client_service_lock(&hosts->net);
    NetClientState state = hosts->net.client.state;
    net_client_service_unlock(&hosts->net);
    return state == NetClientState::connecting || state == NetClientState::waiting;
}

// --- remote play -------------------------------------------------------------------

void hosts_stream_start(Hosts* hosts)
{
    hosts->stream_flags = NET_STREAM_WANT_VIDEO | NET_STREAM_WANT_AUDIO;
    if (hosts->ok && hosts->link == LinkStatus::ready)
    {
        net_client_service_lock(&hosts->net);
        bool asked = net_client_service_set_stream(&hosts->net, hosts->stream_flags);
        hosts->has_tuning_result = false;
        send_tuning(hosts);
        net_client_service_unlock(&hosts->net);
        SDL_Log("Network: remote play %s", asked ? "asked for" : "NOT asked for (not ready)");
    }
}

void hosts_stream_stop(Hosts* hosts)
{
    if (hosts->stream_flags == 0)
    {
        return;
    }
    hosts->stream_flags = 0;
    hosts->has_tuning_result = false;
    hosts->headless_yours = false;   // (the host comes back by itself when the stream stops)
    hosts->headless_on = false;
    if (hosts->ok)
    {
        net_client_service_lock(&hosts->net);
        net_client_service_set_stream(&hosts->net, 0);
        net_client_service_unlock(&hosts->net);
    }
}

bool hosts_take_frame(Hosts* hosts, std::vector<uint8_t>* pixels, uint32_t* width, uint32_t* height,
                      FramePixelFormat* format, uint32_t* frame, NetStreamInfo* info)
{
    return hosts->ok && net_client_service_take_frame(&hosts->net, pixels, width, height, format, frame, info);
}

void hosts_request_devices(Hosts* hosts)
{
    hosts->devices_wanted = true;
    if (hosts->ok)
    {
        net_client_service_lock(&hosts->net);
        send_devices_request(hosts);
        net_client_service_unlock(&hosts->net);
    }
}

void hosts_move_device(Hosts* hosts, const NetDevice* device, int8_t port)
{
    if (!hosts->ok || hosts->link != LinkStatus::ready)
    {
        return;
    }
    uint8_t message[16];
    size_t size = net_settings_write_device_port(message, sizeof(message), device->controller, device->device, port);
    if (size > 0)
    {
        net_client_service_lock(&hosts->net);
        net_client_send(&hosts->net.client, NetChannel::control, message, size);
        net_client_service_unlock(&hosts->net);
        SDL_Log("Network: %s to port %d", device->name.c_str(), port + 1);
    }
}

void hosts_go_headless(Hosts* hosts, bool on)
{
    if (!hosts_streaming(hosts))
    {
        return;
    }
    uint8_t message[4];
    size_t size = net_stream_write_go_headless(message, sizeof(message), on);
    if (size > 0)
    {
        net_client_service_lock(&hosts->net);
        net_client_send(&hosts->net.client, NetChannel::control, message, size);
        net_client_service_unlock(&hosts->net);
        SDL_Log("Network: %s", on ? "GO HEADLESS asked for" : "giving the game back to the host");
    }
}

bool hosts_streaming(const Hosts* hosts)
{
    return hosts->ok && hosts->link == LinkStatus::ready && hosts->stream_flags != 0;
}

void hosts_set_stream_tuning(Hosts* hosts, const NetStreamTuning* tuning)
{
    bool same = hosts->tuning_wanted && hosts->tuning.delay == tuning->delay &&
                hosts->tuning.delay_ms == tuning->delay_ms && hosts->tuning.run_ahead == tuning->run_ahead;
    if (same || !hosts->ok)
    {
        return;
    }
    hosts->tuning = *tuning;
    hosts->tuning_wanted = true;
    net_client_service_lock(&hosts->net);
    send_tuning(hosts);
    net_client_service_unlock(&hosts->net);
}

bool hosts_stream_tuning_result(const Hosts* hosts, NetStreamTuning* tuning)
{
    if (!hosts->has_tuning_result || hosts->stream_flags == 0 || hosts->link != LinkStatus::ready)
    {
        return false;
    }
    *tuning = hosts->tuning_result;
    return true;
}

void hosts_set_frame_queue(Hosts* hosts, uint32_t depth)
{
    if (hosts->ok)
    {
        net_client_service_set_frame_queue(&hosts->net, depth);
    }
}

void hosts_stream_stats(Hosts* hosts, NetClientStreamStats* stats)
{
    *stats = NetClientStreamStats{};
    if (hosts->ok)
    {
        net_client_service_lock(&hosts->net);
        net_client_service_stream_stats(&hosts->net, stats);
        net_client_service_unlock(&hosts->net);
    }
}

int32_t hosts_game_running(const Hosts* hosts)
{
    if (hosts->link != LinkStatus::ready || !hosts->game_known)
    {
        return -1;
    }
    return hosts->game.running ? 1 : 0;
}

bool hosts_pause(const Hosts* hosts, uint32_t* causes)
{
    if (hosts->link != LinkStatus::ready || !hosts->pause_offered)
    {
        *causes = 0;
        return false;
    }
    *causes = hosts->pause_causes;
    return true;
}

void hosts_toggle_pause(Hosts* hosts)
{
    if (!hosts->ok || hosts->link != LinkStatus::ready || !hosts->pause_offered)
    {
        return;
    }
    // A toggle acts on the press; the release keeps press / release paired
    // like every other action.
    uint8_t press[64];
    uint8_t release[64];
    size_t press_size = net_settings_write_action(press, sizeof(press), "pause", true);
    size_t release_size = net_settings_write_action(release, sizeof(release), "pause", false);
    if (press_size == 0 || release_size == 0)
    {
        return;
    }
    net_client_service_lock(&hosts->net);
    net_client_send(&hosts->net.client, NetChannel::control, press, press_size);
    net_client_send(&hosts->net.client, NetChannel::control, release, release_size);
    net_client_service_unlock(&hosts->net);
    SDL_Log("Network: PAUSE sent (%s)", (hosts->pause_causes & NET_PAUSE_USER) != 0 ? "resume" : "pause");
}

int32_t hosts_pad_port(const Hosts* hosts)
{
    return hosts->link == LinkStatus::ready && hosts->pad_port >= 0 ? hosts->pad_port + 1 : 0;
}

PadLink hosts_pad_link(const Hosts* hosts)
{
    PadLink link;
    link.ping_ms = hosts->stats.round_trip_ms;
    link.loss = hosts->stats.loss;
    if (hosts->link == LinkStatus::ready && hosts->has_stats)
    {
        link.state = LinkShown::ready;
    }
    else if (hosts->link == LinkStatus::connecting || hosts->link == LinkStatus::ready)
    {
        link.state = LinkShown::connecting;
    }
    return link;
}

bool hosts_parse_address(const char* text, NetAddress* address)
{
    char host[PAIR_HOST_TEXT];
    std::snprintf(host, sizeof(host), "%s", text);
    uint16_t port = NET_DEFAULT_PORT;
    char* colon = std::strrchr(host, ':');
    if (colon != nullptr)
    {
        *colon = '\0';
        long number = std::strtol(colon + 1, nullptr, 10);
        if (number <= 0 || number > 65535)
        {
            return false;
        }
        port = static_cast<uint16_t>(number);
    }
    return host[0] != '\0' && net_address_from_text(host, port, address);
}

const char* hosts_pair_failure_text(const Hosts* hosts)
{
    if (hosts->refusal_still_known)
    {
        return "REMOVE THIS PHONE ON IT FIRST";
    }
    return hosts_refusal_text(hosts->refusal);
}

const char* hosts_refusal_text(NetClientRefusal refusal)
{
    switch (refusal)
    {
    case NetClientRefusal::none:           return "";
    case NetClientRefusal::not_paired:     return "IT FORGOT THIS PHONE: PAIR AGAIN";
    case NetClientRefusal::pairing_closed: return "IT TAKES NO NEW DEVICES";
    case NetClientRefusal::wrong_code:     return "THE CODE WAS TYPED WRONG TOO OFTEN";
    case NetClientRefusal::refused:        return "IT SAID NO";
    case NetClientRefusal::unknown_host:   return "NOT THE DEVICE YOU PAIRED WITH";
    case NetClientRefusal::connection:     return "CAN'T REACH IT";
    // Across the internet (through the beacon).
    case NetClientRefusal::beacon_unreachable: return "NO ANSWER FROM THE INTERNET SERVER";
    case NetClientRefusal::host_offline:   return "IT ISN'T ONLINE";
    case NetClientRefusal::no_route:       return "COULDN'T CONNECT DIRECTLY TO THE HOST'S NETWORK";
    // Guest links.
    case NetClientRefusal::guest_full:     return "SESSION FULL: ALL SEATS ARE TAKEN";
    case NetClientRefusal::guest_link:     return "THIS SESSION ENDED, OR THE HOST MADE A NEW LINK";
    case NetClientRefusal::guest_kicked:   return "THE HOST REMOVED YOU FROM THIS SESSION";
    }
    return "";
}

const char* hosts_failure_text(const Hosts* hosts)
{
    if (hosts->guest_ended)
    {
        return "THE HOST ENDED THE STREAM";
    }
    return hosts_refusal_text(hosts->refusal);
}

// --- Guest links -------------------------------------------------------------------

bool hosts_set_guest(Hosts* hosts, const char* text)
{
    NetGuestLink link;
    char beacon[NET_GUEST_MAX_BEACON];
    if (!net_guest_link_parse(text, &link, beacon, sizeof(beacon)))
    {
        return false;
    }
    if (hosts->has_guest && std::strcmp(link.token, hosts->guest_link.token) == 0)
    {
        net_guest_link_wipe(&link);
        return true;   // the same link again (the page reloaded, a second tap)
    }
    hosts_clear_guest(hosts);
    hosts->guest_link = link;
    net_guest_link_wipe(&link);
    // The default beacon is named "" like a paired host's.
    hosts->guest_beacon = std::strcmp(beacon, NET_BEACON_DEFAULT_SERVER) == 0 ? "" : beacon;
    hosts->guest_host = KnownHost{};
    hosts->guest_host.name = "GUEST";
    hosts->guest_host.internet = true;
    hosts->guest_host.access = NetAccessLevel::stream;   // remote play, never more
    hosts->guest_host.beacon = hosts->guest_beacon;
    hosts->guest_level = NetGuestLevel::watch;
    hosts->guest_port = NET_GUEST_NO_PORT;
    hosts->has_guest = true;
    SDL_Log("Network: a guest link (room %s)", hosts->guest_link.room);
    return true;
}

void hosts_clear_guest(Hosts* hosts)
{
    if (!hosts->has_guest)
    {
        return;
    }
    if (hosts->linked == HOSTS_GUEST)
    {
        hosts_disconnect(hosts);
    }
    net_guest_link_wipe(&hosts->guest_link);
    hosts->has_guest = false;
}

const KnownHost* hosts_host(const Hosts* hosts, int32_t index)
{
    if (index == HOSTS_GUEST)
    {
        return hosts->has_guest ? &hosts->guest_host : nullptr;
    }
    if (index < 0 || index >= static_cast<int32_t>(hosts->known.size()))
    {
        return nullptr;
    }
    return &hosts->known[static_cast<size_t>(index)];
}
