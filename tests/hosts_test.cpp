// hosts (src/hosts.*) against a real host in the same process (net_access,
// as piccolo runs it, answering speed tests like piccolo's main.cpp) over
// loopback: pairing with the code, a wrong code, connecting again as a
// paired phone with the level the host gives, the ping, a speed test, the
// host online through discovery, FORGET telling the host, the host
// unpairing the phone.
//
//   piccoletto_hosts_test <scratch folder>

#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

#include "host_fs.h"
#include "hosts.h"
#include "net_access.h"
#include "net_settings.h"
#include "net_speed.h"
#include "retropad.h"

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

// The host side: net_access and piccolo's speed test answers.
struct Pump
{
    Hosts*           hosts;
    NetAccess*       access;
    NetSpeedSender   sender;
    NetSpeedReceiver receiver;
    bool             reporting = false;
    uint16_t         last_buttons = 0;   // the newest RetroPad the phone sent
    uint32_t         retropads = 0;      // how many RetroPad packets came
};

// As piccolo's net_speed_request / net_speed_update.
static void host_speed_request(Pump* pump, const NetAccessEvent* event)
{
    uint32_t id = 0;
    NetSpeedDirection direction = NetSpeedDirection::down;
    uint32_t rate = 0;
    uint32_t duration = 0;
    if (!net_speed_read_request(event->data, event->size, &id, &direction, &rate, &duration) || pump->sender.active ||
        pump->reporting || pump->receiver.active)
    {
        return;
    }
    double now = net_seconds();
    if (direction == NetSpeedDirection::down)
    {
        net_speed_sender_start(&pump->sender, id, event->peer, rate, duration, now);
    }
    else
    {
        net_speed_receiver_start(&pump->receiver, id, event->peer,
                                 now + static_cast<double>(duration) / 1000.0 + NET_SPEED_GRACE_SECONDS);
    }
}

static void host_speed_update(Pump* pump)
{
    double now = net_seconds();
    if (pump->sender.active)
    {
        uint8_t packet[NET_SPEED_PACKET_SIZE];
        uint32_t due = net_speed_sender_due(&pump->sender, now);
        for (uint32_t i = 0; i < due; i++)
        {
            size_t size = net_speed_write_data(packet, sizeof(packet), pump->sender.id, pump->sender.sent);
            net_access_send(pump->access, pump->sender.peer, NetChannel::input, packet, size);
            net_speed_sender_sent(&pump->sender, size);
        }
        pump->reporting = !pump->sender.active;
    }
    uint8_t out[64];
    if (pump->reporting)
    {
        NetSpeedReport report;
        report.id = pump->sender.id;
        report.direction = NetSpeedDirection::down;
        report.packets = pump->sender.sent;
        report.bytes = pump->sender.sent_bytes;
        report.seconds = pump->sender.duration;
        size_t size = net_speed_write_report(out, sizeof(out), &report);
        net_access_send(pump->access, pump->sender.peer, NetChannel::control, out, size);
        pump->reporting = false;
    }
    if (pump->receiver.active && now >= pump->receiver.until)
    {
        NetSpeedReport report;
        net_speed_receiver_report(&pump->receiver, NetSpeedDirection::up, &report);
        size_t size = net_speed_write_report(out, sizeof(out), &report);
        net_access_send(pump->access, pump->receiver.peer, NetChannel::control, out, size);
        pump->receiver.active = false;
    }
}

static void pump_once(Pump* pump)
{
    NetAccessEvent event;
    while (net_access_poll(pump->access, &event))
    {
        if (event.type != NetAccessEventType::message)
        {
            continue;
        }
        uint32_t id = 0;
        uint8_t pad = 0;
        NetRetroPadState states[NET_INPUT_HISTORY];
        uint32_t count = 0;
        if (event.channel == NetChannel::control && event.size > 0 && event.data[0] == NET_SPEED_REQUEST)
        {
            host_speed_request(pump, &event);
        }
        else if (event.channel == NetChannel::control &&
                 net_settings_read_controllers_request(event.data, event.size, &id))
        {
            // As piccolo: the phone's pad 0 plays on port 1.
            NetControllers controllers;
            controllers.pad_ports[0] = 0;
            uint8_t out[256];
            size_t size = net_settings_write_controllers(out, sizeof(out), id, &controllers);
            net_access_send(pump->access, event.peer, NetChannel::control, out, size);
        }
        else if (event.channel == NetChannel::input && net_input_read_retropad(event.data, event.size, &pad, states, &count))
        {
            pump->last_buttons = states[count - 1].buttons;   // oldest first
            pump->retropads++;
        }
        else if (event.channel == NetChannel::input)
        {
            net_speed_receive(&pump->receiver, event.data, event.size, net_seconds());
        }
    }
    host_speed_update(pump);
    hosts_update(pump->hosts, now_seconds());
    SDL_Delay(1);
}

static bool pump_until_pair_done(Pump* pump)
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

// Pumps until the speed test is over (or `seconds` pass).
static void end_wait(Pump* pump, double seconds)
{
    double end = now_seconds() + seconds;
    while (now_seconds() < end && pump->hosts->speed.running)
    {
        pump_once(pump);
    }
}

static bool pump_until_link_done(Pump* pump)
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
    Pump pump;
    pump.hosts = hosts;
    pump.access = access;

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

    // Connecting again as a paired phone (no code), still at gamepad.
    for (int32_t i = 0; i < 20; i++)
    {
        pump_once(&pump);
    }
    check(access->trust.devices.size() == 1, "the host lists the phone");
    check(hosts_connect(hosts, 0), "connect starts");
    check(pump_until_link_done(&pump) && hosts->link == LinkStatus::ready, "connected as a paired phone");
    for (int32_t i = 0; i < 20; i++)
    {
        pump_once(&pump);
    }
    check(hosts->has_stats, "the ping while connected");
    uint32_t ping_ms = 0;
    check(hosts_link_shown(hosts, &ping_ms) == LinkShown::ready, "the status shows the ping");
    check(hosts_pad_port(hosts) == 1, "the pad's port from the host's controllers");

    // The pad: A and START held reach the host as a RetroPad.
    RetroPad pad;
    retropad_press(&pad, RetroPadButton::a);
    retropad_press(&pad, RetroPadButton::start);
    for (int32_t i = 0; i < 20; i++)
    {
        hosts_send_pad(hosts, &pad);
        pump_once(&pump);
    }
    uint16_t expected = static_cast<uint16_t>((1u << 8) | (1u << 3));
    check(pump.retropads > 0 && pump.last_buttons == expected, "the RetroPad reaches the host");
    check(!hosts->speed.running && !hosts->known[0].measured, "no speed test at gamepad level");
    hosts_disconnect(hosts);
    check(hosts_pad_port(hosts) == 0, "no port once left");
    check(hosts_link_shown(hosts, &ping_ms) == LinkShown::offline, "offline once left");

    // The host raises our level: the next connection brings it, and runs the
    // speed test (STREAM access or more, once per run).
    if (access->trust.devices.size() == 1)
    {
        net_access_set_level(access, 0, NetAccessLevel::full);
    }
    check(hosts_connect(hosts, 0), "connect again");
    check(pump_until_link_done(&pump) && hosts->link == LinkStatus::ready, "connected again");
    check(!hosts->known.empty() && hosts->known[0].access == NetAccessLevel::full, "the host's level");
    check(hosts->speed.running, "the speed test runs");
    end_wait(&pump, 20.0);
    check(!hosts->speed.running, "the speed test ends");
    check(!hosts->known.empty() && hosts->known[0].down_kbps > 0 && hosts->known[0].up_kbps > 0, "speeds measured");
    hosts_disconnect(hosts);

    // Discovery: the host answers at its last address, so it's online.
    double end = now_seconds() + 5.0;
    while (now_seconds() < end && !(hosts->known.size() == 1 && hosts->known[0].online))
    {
        pump_once(&pump);
    }
    check(hosts->known.size() == 1 && hosts->known[0].online, "online through discovery");
    // (Only ours: a real host on the default port may answer the broadcast too.)
    bool offered = false;
    for (int32_t i = 0; i < hosts_unpaired_count(hosts); i++)
    {
        offered = offered || hosts_unpaired(hosts, i)->address.port == TEST_PORT;
    }
    check(!offered, "a paired host isn't offered for pairing");

    // What was saved comes back.
    hosts_destroy(hosts);
    delete hosts;
    hosts = new Hosts{};
    pump.hosts = hosts;
    check(hosts_create(hosts, phone_dir.c_str(), "TEST PHONE"), "hosts_create again");
    check(hosts->known.size() == 1 && hosts->known[0].access == NetAccessLevel::full &&
              hosts->known[0].address.port == TEST_PORT,
          "paired host, level and address kept");

    check(hosts->known.size() == 1 && hosts->known[0].down_kbps > 0, "speeds kept");

    // FORGET with the host online: it's told and forgets this phone too.
    end = now_seconds() + 5.0;
    while (now_seconds() < end && !(hosts->known.size() == 1 && hosts->known[0].online))
    {
        pump_once(&pump);
    }
    hosts_forget(hosts, 0);
    end = now_seconds() + 5.0;
    while (now_seconds() < end && !(hosts->known.empty() && access->trust.devices.empty()))
    {
        pump_once(&pump);
    }
    check(hosts->known.empty(), "forgotten here");
    check(access->trust.devices.empty(), "forgotten on the host too");

    // Paired again; the host unpairs the phone while it's connected.
    for (int32_t i = 0; i < 50; i++)
    {
        pump_once(&pump);
    }
    if (!access->pairing_open)
    {
        check(net_access_open_pairing(access), "host opens pairing for the next one");
    }
    check(hosts_pair(hosts, address, access->code) && pump_until_pair_done(&pump) && hosts->pair == PairStatus::paired,
          "paired again");
    check(hosts_connect(hosts, 0) && pump_until_link_done(&pump) && hosts->link == LinkStatus::ready, "connected");
    for (int32_t i = 0; i < 20; i++)
    {
        pump_once(&pump);
    }
    check(access->trust.devices.size() == 1, "the host lists it again");
    if (access->trust.devices.size() == 1)
    {
        net_access_remove_device(access, 0);
    }
    end = now_seconds() + 5.0;
    while (now_seconds() < end && !hosts->known.empty())
    {
        pump_once(&pump);
    }
    check(hosts->known.empty(), "unpaired by the host: gone here too");
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
