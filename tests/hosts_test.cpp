// hosts (src/hosts.*) against a real host in the same process (net_access,
// as piccolo runs it) over loopback: pairing with the code, a wrong code,
// connecting again as a paired phone with the level the host gives, the host
// online through discovery, forgetting it.
//
//   piccoletto_hosts_test <scratch folder>

#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

#include "host_fs.h"
#include "hosts.h"
#include "net_access.h"

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("FAILED: %s\n", what);
        g_failures++;
    }
}

constexpr uint16_t TEST_PORT = 55499;

static double now_seconds()
{
    return static_cast<double>(SDL_GetTicksNS()) / 1e9;
}

// Runs both sides until `done` says so (or a few seconds pass).
struct Pump
{
    Hosts*     hosts;
    NetAccess* access;
};

static void pump_once(const Pump* pump)
{
    NetAccessEvent event;
    while (net_access_poll(pump->access, &event))
    {
    }
    hosts_update(pump->hosts, now_seconds());
    SDL_Delay(2);
}

static bool pump_until_pair_done(const Pump* pump)
{
    double end = now_seconds() + 5.0;
    while (now_seconds() < end)
    {
        pump_once(pump);
        if (pump->hosts->pair != PairStatus::busy)
        {
            return true;
        }
    }
    return false;
}

static bool pump_until_link_done(const Pump* pump)
{
    double end = now_seconds() + 5.0;
    while (now_seconds() < end)
    {
        pump_once(pump);
        if (pump->hosts->link != LinkStatus::connecting)
        {
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: piccoletto_hosts_test <scratch folder>\n");
        return 2;
    }
    std::string root = argv[1];
    std::string phone_dir = root + "/phone";
    std::string host_dir = root + "/host";
    check(host_fs_make_directories(phone_dir.c_str()) && host_fs_make_directories(host_dir.c_str()), "scratch folders");

    // The host: piccolo's side, pairing open.
    check(net_init(), "net_init");
    NetKeypair host_identity;
    std::string host_identity_path = host_dir + "/identity.cfg";
    std::string host_devices_path = host_dir + "/devices.cfg";
    check(net_identity_load_or_create(host_identity_path.c_str(), &host_identity), "host identity");
    NetAccess* access = new NetAccess{};
    check(net_access_create(access, TEST_PORT, "TEST HOST", &host_identity, host_devices_path.c_str()), "host listens");

    Hosts* hosts = new Hosts{};
    check(hosts_create(hosts, phone_dir.c_str(), "TEST PHONE"), "hosts_create");
    check(hosts->known.empty(), "nothing paired at first");
    Pump pump = {hosts, access};

    NetAddress address;
    char typed[32];
    std::snprintf(typed, sizeof(typed), "127.0.0.1:%u", static_cast<unsigned>(TEST_PORT));
    check(hosts_parse_address(typed, &address) && address.port == TEST_PORT, "typed address with a port");
    check(!hosts_parse_address("127.0.0.1:99999", &address), "a port out of range");

    // Pairing closed: the host says so.
    check(hosts_parse_address(typed, &address), "address");
    check(hosts_pair(hosts, address, "123456"), "pair starts");
    check(pump_until_pair_done(&pump) && hosts->pair == PairStatus::failed &&
              hosts->refusal == NetClientRefusal::pairing_closed,
          "pairing closed on the host");

    // A wrong code.
    check(net_access_open_pairing(access), "host opens pairing");
    char wrong[NET_PAIR_CODE_DIGITS + 1];
    std::snprintf(wrong, sizeof(wrong), "%s", access->code);
    wrong[0] = wrong[0] == '9' ? '0' : static_cast<char>(wrong[0] + 1);
    hosts->pair = PairStatus::idle;
    check(hosts_pair(hosts, address, wrong), "pair with a wrong code starts");
    check(pump_until_pair_done(&pump) && hosts->pair == PairStatus::failed &&
              hosts->refusal == NetClientRefusal::wrong_code,
          "wrong code refused");

    // The right code: a failed attempt gives a new one, once the host has
    // seen the attempt end (the connection closing too).
    for (int32_t i = 0; i < 100; i++)
    {
        pump_once(&pump);
    }
    if (!access->pairing_open)
    {
        check(net_access_open_pairing(access), "host opens pairing again");
    }
    hosts->pair = PairStatus::idle;
    check(hosts_pair(hosts, address, access->code), "pair starts");
    check(pump_until_pair_done(&pump) && hosts->pair == PairStatus::paired, "paired");
    check(hosts->known.size() == 1 && hosts->paired_index == 0, "one paired host");
    if (!hosts->known.empty())
    {
        check(hosts->known[0].name == "TEST HOST", "the host's name");
        check(hosts->known[0].address.port == TEST_PORT, "its address kept");
        check(hosts->known[0].access == NetAccessLevel::gamepad, "new devices start at gamepad");
    }

    // The host raises our level; connecting again (no code) brings it.
    for (int32_t i = 0; i < 20; i++)
    {
        pump_once(&pump);
    }
    check(access->trust.devices.size() == 1, "the host lists the phone");
    if (access->trust.devices.size() == 1)
    {
        net_access_set_level(access, 0, NetAccessLevel::full);
    }
    check(hosts_connect(hosts, 0), "connect starts");
    check(pump_until_link_done(&pump) && hosts->link == LinkStatus::ready, "connected as a paired phone");
    check(!hosts->known.empty() && hosts->known[0].access == NetAccessLevel::full, "the host's level");
    hosts_disconnect(hosts);

    // Discovery: the host answers at its last address, so it's online.
    double end = now_seconds() + 5.0;
    while (now_seconds() < end && !(hosts->known.size() == 1 && hosts->known[0].online))
    {
        pump_once(&pump);
    }
    check(hosts->known.size() == 1 && hosts->known[0].online, "online through discovery");
    check(hosts_unpaired_count(hosts) == 0, "a paired host isn't offered for pairing");

    // What was saved comes back.
    hosts_destroy(hosts);
    delete hosts;
    hosts = new Hosts{};
    pump.hosts = hosts;
    check(hosts_create(hosts, phone_dir.c_str(), "TEST PHONE"), "hosts_create again");
    check(hosts->known.size() == 1 && hosts->known[0].access == NetAccessLevel::full &&
              hosts->known[0].address.port == TEST_PORT,
          "paired host, level and address kept");

    // Forgetting.
    hosts_forget(hosts, 0);
    check(hosts->known.empty(), "forgotten");
    hosts_destroy(hosts);
    delete hosts;

    net_access_destroy(access);
    delete access;
    net_keypair_wipe(&host_identity);
    net_shutdown();

    if (g_failures == 0)
    {
        std::printf("hosts: all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
