#include "hosts.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_log.h>

#include "net_pairing.h"

// Discovery: a broadcast (and a unicast to every paired host's last address,
// for networks that drop broadcasts) this often; a host that stops answering
// is gone after a few missed rounds.
constexpr double DISCOVERY_SECONDS = 2.0;
constexpr double FOUND_SECONDS     = 7.0;
constexpr size_t PAIR_HOST_TEXT    = 64;    // a typed address

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
    if (event->newly_paired)
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
        case NetClientEventType::message:   // nothing read yet (game state, controllers: later)
            break;
        case NetClientEventType::ready:
            on_ready(hosts, &event, hosts->connecting_to);
            break;
        case NetClientEventType::access:
            if (hosts->linked >= 0 && hosts->linked < static_cast<int32_t>(hosts->known.size()))
            {
                hosts->known[static_cast<size_t>(hosts->linked)].access = event.access;
                write_info(hosts, &hosts->known[static_cast<size_t>(hosts->linked)]);
            }
            break;
        case NetClientEventType::refused:
            SDL_Log("Network: refused (%s)", hosts_refusal_text(event.refusal));
            hosts->refusal = event.refusal;
            if (hosts->pair == PairStatus::busy)
            {
                hosts->pair = PairStatus::failed;
            }
            if (hosts->link == LinkStatus::connecting)
            {
                hosts->link = LinkStatus::failed;
            }
            break;
        case NetClientEventType::closed:
            if (hosts->link == LinkStatus::ready)
            {
                hosts->link = LinkStatus::failed;
                hosts->refusal = NetClientRefusal::connection;
            }
            break;
        }
    }
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
        return true;
    }
    hosts_disconnect(hosts);
    NetAddress address = hosts->known[static_cast<size_t>(index)].address;
    if (address.port == 0 || hosts->client.state != NetClientState::idle ||
        !net_client_connect(&hosts->client, address, nullptr))
    {
        hosts->link = LinkStatus::failed;
        hosts->refusal = NetClientRefusal::connection;
        return false;
    }
    hosts->link = LinkStatus::connecting;
    hosts->linked = index;
    hosts->refusal = NetClientRefusal::none;
    hosts->connecting_to = address;
    return true;
}

void hosts_disconnect(Hosts* hosts)
{
    if (hosts->ok)
    {
        net_client_close(&hosts->client);
    }
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
    if (hosts->linked == index)
    {
        hosts_disconnect(hosts);
    }
    std::string section = key_section(hosts->known[static_cast<size_t>(index)].key);
    settings_file_remove(&hosts->info, section.c_str(), "address");
    settings_file_remove(&hosts->info, section.c_str(), "port");
    settings_file_remove(&hosts->info, section.c_str(), "access");
    settings_file_save(&hosts->info, hosts->info_path.c_str(), "piccoletto's paired hosts: last address and access level");
    net_trust_remove(&hosts->client.trust, static_cast<uint32_t>(index));
    net_trust_save(&hosts->client.trust, hosts->client.trust_path.c_str());
    rebuild_known(hosts);
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

const char* hosts_refusal_text(NetClientRefusal refusal)
{
    switch (refusal)
    {
    case NetClientRefusal::none:           return "";
    case NetClientRefusal::not_paired:     return "IT DOESN'T KNOW THIS PHONE: PAIR AGAIN";
    case NetClientRefusal::pairing_closed: return "OPEN PAIR A DEVICE ON IT FIRST";
    case NetClientRefusal::wrong_code:     return "WRONG CODE";
    case NetClientRefusal::unknown_host:   return "NOT THE DEVICE YOU PAIRED WITH";
    case NetClientRefusal::connection:     return "CAN'T REACH IT";
    }
    return "";
}
