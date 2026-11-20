#include "hosts.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_log.h>

#include "net_pairing.h"
#include "net_settings.h"
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
    return a.host == b.host && a.port == b.port;
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
    for (size_t i = 0; i < hosts->client.trust.devices.size(); i++)
    {
        const NetTrustedDevice* device = &hosts->client.trust.devices[i];
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
    bool created = net_client_create(&hosts->client, &identity, phone_name, trust_path.c_str()) &&
                   net_host_create_client(&hosts->discovery);
    net_keypair_wipe(&identity);
    if (!created)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Network: no socket; pairing and playing are off");
        net_client_destroy(&hosts->client);
        net_host_destroy(&hosts->discovery);
        net_shutdown();
        return false;
    }
    rebuild_known(hosts);
    hosts->ok = true;
    char fingerprint[64];
    net_key_fingerprint(hosts->client.our_key, fingerprint, sizeof(fingerprint));
    SDL_Log("Network: this phone is %s, %zu paired host(s)", fingerprint, hosts->known.size());
    return true;
}

void hosts_destroy(Hosts* hosts)
{
    if (!hosts->ok)
    {
        return;
    }
    net_client_close(&hosts->client);
    net_client_destroy(&hosts->client);
    net_host_destroy(&hosts->discovery);
    net_shutdown();
    hosts->ok = false;
}

void hosts_set_name(Hosts* hosts, const char* phone_name)
{
    // As net_client_create keeps it: cut the way the messages carry it.
    uint8_t bytes[NET_DEVICE_NAME_MAX];
    size_t size = net_pair_name_bytes(phone_name, bytes);
    hosts->client.name.assign(reinterpret_cast<const char*>(bytes), size);
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
    if (event->newly_paired)
    {
        rebuild_known(hosts);
    }
    if (event->device < 0 || event->device >= static_cast<int32_t>(hosts->known.size()))
    {
        return;
    }
    KnownHost* host = &hosts->known[static_cast<size_t>(event->device)];
    host->address = address;
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
        net_client_close(&hosts->client);
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
    net_client_unpair(&hosts->client, static_cast<uint32_t>(index));   // not connected: only our list
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
    if (!net_client_send(&hosts->client, NetChannel::control, out, size))
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
            net_client_send(&hosts->client, NetChannel::input, packet, size);
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
        }
    }
}

void hosts_update(Hosts* hosts, double now)
{
    if (!hosts->ok)
    {
        return;
    }
    discover(hosts, now);
    NetClientEvent event;
    while (net_client_poll(&hosts->client, &event))
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
                    net_client_unpair(&hosts->client, static_cast<uint32_t>(index));
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
                uint8_t request[16];
                size_t size = net_settings_write_controllers_request(request, sizeof(request), hosts->next_request++);
                net_client_send(&hosts->client, NetChannel::control, request, size);
                const KnownHost* host = &hosts->known[static_cast<size_t>(hosts->linked)];
                if (event.access != NetAccessLevel::gamepad && !host->measured)
                {
                    speed_start(hosts);
                }
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
            if (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
            {
                KnownHost* host = &hosts->known[static_cast<size_t>(hosts->linked)];
                host->access = event.access;
                write_info(hosts, host);
                // Raised to STREAM while connected: measure now.
                if (event.access != NetAccessLevel::gamepad && !host->measured && !hosts->speed.running)
                {
                    speed_start(hosts);
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
            if (hosts->link == LinkStatus::connecting)
            {
                hosts->link = LinkStatus::failed;
            }
            break;
        case NetClientEventType::closed:
            hosts->speed.running = false;
            if (hosts->link == LinkStatus::ready)
            {
                hosts->link = LinkStatus::failed;
                hosts->refusal = NetClientRefusal::connection;
            }
            break;
        }
    }
    if (hosts->connect_pending && hosts->client.state == NetClientState::idle)
    {
        hosts->connect_pending = false;
        if (!net_client_connect(&hosts->client, hosts->connecting_to, nullptr))
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
        hosts->has_stats = net_client_stats(&hosts->client, &hosts->stats);
    }
    speed_update(hosts);
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

bool hosts_pair(Hosts* hosts, NetAddress address, const char* code)
{
    if (!hosts->ok || hosts->client.state != NetClientState::idle)
    {
        return false;
    }
    if (!net_client_connect(&hosts->client, address, code))
    {
        return false;
    }
    hosts->pair = PairStatus::busy;
    hosts->paired_index = -1;
    hosts->refusal = NetClientRefusal::none;
    hosts->refusal_still_known = false;
    hosts->connecting_to = address;
    return true;
}

bool hosts_connect(Hosts* hosts, int32_t index)
{
    if (!hosts->ok || index < 0 || index >= static_cast<int32_t>(hosts->known.size()))
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
    NetAddress address = hosts->known[static_cast<size_t>(index)].address;
    if (address.port == 0)
    {
        hosts->link = LinkStatus::failed;
        hosts->refusal = NetClientRefusal::connection;
        return false;
    }
    hosts->link = LinkStatus::connecting;
    hosts->linked = index;
    hosts->refusal = NetClientRefusal::none;
    hosts->connecting_to = address;
    // The last connection may still be closing (HOME, then a mode right
    // away): then hosts_update connects once it's gone.
    hosts->connect_pending = hosts->client.state != NetClientState::idle;
    if (!hosts->connect_pending && !net_client_connect(&hosts->client, address, nullptr))
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
        net_client_close(&hosts->client);
    }
    hosts->speed.running = false;
    hosts->has_stats = false;
    hosts->connect_pending = false;
    hosts->link = LinkStatus::idle;
    hosts->linked = -1;
}

void hosts_cancel(Hosts* hosts)
{
    hosts_disconnect(hosts);
    hosts->pair = PairStatus::idle;
}

void hosts_forget(Hosts* hosts, int32_t index)
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
        net_client_unpair(&hosts->client, static_cast<uint32_t>(index));
        hosts->link = LinkStatus::idle;
        hosts->linked = -1;
        forget_info(hosts, key);
        rebuild_known(hosts);
        return;
    }
    // Reachable: connect to tell it (hosts_update finishes it); else only here.
    const KnownHost* host = &hosts->known[static_cast<size_t>(index)];
    if (host->online && host->address.port != 0 && hosts->client.state == NetClientState::idle &&
        net_client_connect(&hosts->client, host->address, nullptr))
    {
        hosts->pending_unpair = index;
        hosts->connecting_to = host->address;
        return;
    }
    forget_here(hosts, index);
}

void hosts_send_pad(Hosts* hosts, const RetroPad* pad)
{
    if (!hosts->ok || hosts->link != LinkStatus::ready)
    {
        return;
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
        net_client_send(&hosts->client, NetChannel::input, packet, size);
    }
}

int32_t hosts_pad_port(const Hosts* hosts)
{
    return hosts->link == LinkStatus::ready && hosts->pad_port >= 0 ? hosts->pad_port + 1 : 0;
}

LinkShown hosts_link_shown(const Hosts* hosts, uint32_t* ping_ms)
{
    *ping_ms = hosts->stats.round_trip_ms;
    if (hosts->link == LinkStatus::ready && hosts->has_stats)
    {
        return LinkShown::ready;
    }
    if (hosts->link == LinkStatus::connecting || hosts->link == LinkStatus::ready)
    {
        return LinkShown::connecting;
    }
    return LinkShown::offline;
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
    case NetClientRefusal::pairing_closed: return "OPEN PAIR A DEVICE ON IT FIRST";
    case NetClientRefusal::wrong_code:     return "WRONG CODE";
    case NetClientRefusal::unknown_host:   return "NOT THE DEVICE YOU PAIRED WITH";
    case NetClientRefusal::connection:     return "CAN'T REACH IT";
    }
    return "";
}
