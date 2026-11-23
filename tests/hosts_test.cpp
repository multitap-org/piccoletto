// hosts (src/hosts.*) against a real host in the same process (net_access,
// as piccolo runs it, answering speed tests like piccolo's main.cpp) over
// loopback: pairing with the code, a wrong code, connecting again as a
// paired phone with the level the host gives, the ping, a speed test, the
// host online through discovery, FORGET telling the host, the host
// unpairing the phone; REMOTE PLAY's stream (stream_start, game_state,
// stream_info, a coded frame arriving exact, stream_stop); DEVICES (the
// host's list, moving one, the list again); across the internet: pairing
// with the host's pair link through a beacon in the same process, then
// connecting through it (the host isn't on the LAN for a host paired so).
//
//   piccoletto_hosts_test <scratch folder>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "beacon_net.h"
#include "beacon_server.h"
#include "host_fs.h"
#include "hosts.h"
#include "net_access.h"
#include "net_content.h"
#include "net_pair_link.h"
#include "net_stream.h"
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
    bool             streaming = false;  // the phone asked for the stream (stream_start, no stop since)
    uint32_t         stream_peer = 0;
    uint8_t          stream_flags = 0;
    NetStreamDelay   tuning_asked = NetStreamDelay::host;   // the last stream_tuning's delay mode
    bool             headless = false;   // GO HEADLESS granted
    std::vector<NetDevice> devices;     // as piccolo's list: who plays on its ports
    uint32_t         devices_requests = 0;
    BeaconServer*    beacon_server = nullptr;   // across the internet (the last part)
    BeaconNet*       beacon_net = nullptr;
    // Pairing requests: the host's user types the phone's code (read from the
    // phone, as a person would), at `grant`; `wrong_once` types it wrong the
    // first time (the phone makes a new one and asks again).
    NetAccessLevel   grant = NetAccessLevel::gamepad;
    bool             wrong_once = false;
    uint32_t         requests = 0;
    std::string      first_code;         // the code the first request of a pairing showed
    std::string      last_code;          // and the last one
};

static void host_send_devices(Pump* pump, uint32_t peer, uint32_t id)
{
    std::vector<uint8_t> out(1024);
    size_t size = net_settings_write_devices(out.data(), out.size(), id, &pump->devices);
    net_access_send(pump->access, peer, NetChannel::control, out.data(), size);
}

// The stream's picture: 40x30 RGB565 with a pattern that changes with `n`.
constexpr uint32_t STREAM_WIDTH = 40;
constexpr uint32_t STREAM_HEIGHT = 30;

static void stream_picture(std::vector<uint8_t>* pixels, uint32_t n)
{
    pixels->resize(STREAM_WIDTH * STREAM_HEIGHT * 2);
    for (size_t i = 0; i < pixels->size(); i++)
    {
        (*pixels)[i] = static_cast<uint8_t>((i * 7 + n * 13) & 0xFF);
    }
}

// As piccolo's net_service, for one frame: stream_info, the frame coded as a
// keyframe in chunks, and a piece of audio.
static void host_stream_frame(Pump* pump, uint32_t n, const std::vector<uint8_t>* pixels)
{
    NetStreamInfo info;
    info.width = STREAM_WIDTH;
    info.height = STREAM_HEIGHT;
    info.format = FramePixelFormat::rgb565;
    info.aspect = 4.0 / 3.0;
    info.fps = 60.0;
    info.sample_rate = 32040.0;
    std::vector<uint8_t> packet(NET_STREAM_VIDEO_HEADER + NET_STREAM_CHUNK + 64);
    size_t size = net_stream_write_info(packet.data(), packet.size(), &info);
    net_access_send(pump->access, pump->stream_peer, NetChannel::control, packet.data(), size);
    FrameDeltaEncoder encoder;
    std::vector<uint8_t> coded(frame_delta_max_size(STREAM_WIDTH, STREAM_HEIGHT, FramePixelFormat::rgb565));
    size_t coded_size = frame_delta_encode(&encoder, pixels->data(), STREAM_WIDTH, STREAM_HEIGHT,
                                           FramePixelFormat::rgb565, true, coded.data());
    uint32_t chunks = net_stream_chunk_count(coded_size);
    for (uint32_t chunk = 0; chunk < chunks; chunk++)
    {
        size = net_stream_write_video(packet.data(), packet.size(), n, true, coded.data(), coded_size, chunk);
        net_access_send(pump->access, pump->stream_peer, NetChannel::stream, packet.data(), size);
    }
    int16_t samples[2 * 64] = {};
    std::vector<uint8_t> audio(NET_STREAM_AUDIO_HEADER + sizeof(samples));
    size = net_stream_write_audio(audio.data(), audio.size(), n, info.sample_rate, samples, 0, 64);
    net_access_send(pump->access, pump->stream_peer, NetChannel::stream, audio.data(), size);
}

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
    if (pump->beacon_server != nullptr)
    {
        uint64_t now = beacon_now_ms();
        beacon_net_drain(pump->beacon_net, pump->beacon_server, now, BeaconSocket::PRIMARY, 64);
        beacon_net_drain(pump->beacon_net, pump->beacon_server, now, BeaconSocket::SECONDARY, 64);
        beacon_server_tick(pump->beacon_server, now);
    }
    NetAccessEvent event;
    while (net_access_poll(pump->access, &event))
    {
        if (event.type == NetAccessEventType::pairing_requested)
        {
            char code[NET_PAIR_CODE_DIGITS + 1] = {};
            hosts_pair_code(pump->hosts, code);
            pump->requests++;
            if (pump->requests == 1)
            {
                pump->first_code = code;
            }
            pump->last_code = code;
            if (pump->wrong_once)
            {
                pump->wrong_once = false;
                code[NET_PAIR_CODE_DIGITS - 1] = code[NET_PAIR_CODE_DIGITS - 1] == '9' ? '0' : static_cast<char>(code[NET_PAIR_CODE_DIGITS - 1] + 1);
            }
            net_access_answer_pairing(pump->access, event.peer, code, pump->grant);
            continue;
        }
        if (event.type != NetAccessEventType::message)
        {
            continue;
        }
        uint32_t id = 0;
        uint8_t pad = 0;
        NetRetroPadState states[NET_INPUT_HISTORY];
        uint32_t count = 0;
        uint8_t flags = 0;
        if (event.channel == NetChannel::control && event.size > 0 && event.data[0] == NET_SPEED_REQUEST)
        {
            host_speed_request(pump, &event);
        }
        else if (event.channel == NetChannel::control && event.size > 0 && event.data[0] == NET_STREAM_START &&
                 net_stream_read_start(event.data, event.size, &flags))
        {
            pump->streaming = true;
            pump->stream_peer = event.peer;
            pump->stream_flags = flags;
            // As piccolo: a game runs.
            NetGameState game;
            game.running = true;
            game.core_name = "Test";
            uint8_t out[256];
            size_t size = net_content_write_game_state(out, sizeof(out), &game);
            net_access_send(pump->access, event.peer, NetChannel::control, out, size);
        }
        else if (event.channel == NetChannel::control &&
                 (net_settings_read_devices_request(event.data, event.size, &id) ||
                  net_settings_read_devices_watch(event.data, event.size, &id)))   // (as piccolo: any level reads)
        {
            pump->devices_requests++;
            host_send_devices(pump, event.peer, id);
        }
        else if (event.channel == NetChannel::control && event.size > 0 &&
                 event.data[0] == static_cast<uint8_t>(NetSettingsMessage::device_port))
        {
            // As piccolo: on that port only, then the list again (id 0).
            uint32_t controller = 0;
            uint32_t device = 0;
            int8_t port = -1;
            if (net_settings_read_device_port(event.data, event.size, &controller, &device, &port))
            {
                for (size_t i = 0; i < pump->devices.size(); i++)
                {
                    if (pump->devices[i].controller == controller && pump->devices[i].device == device)
                    {
                        pump->devices[i].ports = static_cast<uint8_t>(port >= 0 ? 1u << port : 0u);
                    }
                }
                host_send_devices(pump, event.peer, 0);
            }
        }
        else if (event.channel == NetChannel::control && event.size > 0 && event.data[0] == NET_STREAM_GO_HEADLESS)
        {
            // As piccolo: granted while streaming, the state back.
            bool on = false;
            if (net_stream_read_go_headless(event.data, event.size, &on))
            {
                pump->headless = on && pump->streaming;
                uint8_t out[4];
                size_t size = net_stream_write_headless_state(out, sizeof(out), pump->headless, pump->headless);
                net_access_send(pump->access, event.peer, NetChannel::control, out, size);
            }
        }
        else if (event.channel == NetChannel::control && event.size > 0 && event.data[0] == NET_STREAM_TUNING)
        {
            // As piccolo: AUTO asked, 7 ms in use for now.
            NetStreamTuning asked;
            if (net_stream_read_tuning(event.data, event.size, NET_STREAM_TUNING, &asked))
            {
                pump->tuning_asked = asked.delay;
                NetStreamTuning result = asked;
                result.delay_ms = 7;
                uint8_t out[8];
                size_t size = net_stream_write_tuning(out, sizeof(out), NET_STREAM_TUNING_RESULT, &result);
                net_access_send(pump->access, event.peer, NetChannel::control, out, size);
            }
        }
        else if (event.channel == NetChannel::control && event.size == 1 && event.data[0] == NET_STREAM_STOP)
        {
            pump->streaming = false;
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

// Until the connected host's level is `level` (told live, no reconnect).
static bool pump_until_access(Pump* pump, NetAccessLevel level)
{
    double end = now_seconds() + 5.0;
    while (now_seconds() < end)
    {
        pump_once(pump);
        if (!pump->hosts->known.empty() && pump->hosts->known[0].access == level)
        {
            return true;
        }
    }
    return false;
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

    // A guest link: the host at HOSTS_GUEST, outside the paired list, never saved.
    check(!hosts_set_guest(hosts, "https://beacon.get-piccolo.app/join/7hqelxhbgx2e4#abcdefghijklmnopqrstuvwxyz"),
          "a link whose room doesn't match its token isn't taken");
    check(hosts_set_guest(hosts, "piccoletto://join/7hqelxhbgx2e3?t=abcdefghijklmnopqrstuvwxyz&b=my.beacon"),
          "a guest link from Android's intent");
    const KnownHost* guest = hosts_host(hosts, HOSTS_GUEST);
    check(guest != nullptr && guest->access == NetAccessLevel::stream && hosts->guest_beacon == "my.beacon",
          "its host may stream, through the link's beacon");
    check(hosts->known.empty() && !host_fs_exists((phone_dir + "/net_hosts.cfg").c_str()), "nothing paired or saved");
    hosts_clear_guest(hosts);
    check(hosts_host(hosts, HOSTS_GUEST) == nullptr, "cleared");
    check(hosts_set_guest(hosts, "https://get-piccolo.app/play/piccoletto/#join=7hqelxhbgx2e3.abcdefghijklmnopqrstuvwxyz") &&
              hosts->guest_beacon.empty(),
          "piccoletto web's hash, the default beacon");
    hosts_clear_guest(hosts);
    Pump pump;
    pump.hosts = hosts;
    pump.access = access;

    NetAddress address;
    char typed[32];
    std::snprintf(typed, sizeof(typed), "127.0.0.1:%u", static_cast<unsigned>(TEST_PORT));
    check(hosts_parse_address(typed, &address) && address.port == TEST_PORT, "typed address with a port");
    check(!hosts_parse_address("127.0.0.1:99999", &address), "a port out of range");

    // The phone shows its code; the host's user types it (the pump).
    char code[NET_PAIR_CODE_DIGITS + 1];
    check(!hosts_pair(hosts, address, "123456"), "a code that isn't 9 digits doesn't start");

    // The host takes no new devices: it says so.
    access->accept_pairing = false;
    net_pair_code_generate(code);
    check(hosts_pair(hosts, address, code), "pair starts");
    check(pump_until_pair_done(&pump) && hosts->pair == PairStatus::failed &&
              hosts->refusal == NetClientRefusal::pairing_closed,
          "no new devices on the host");
    access->accept_pairing = true;
    for (int32_t i = 0; i < 50; i++)   // that connection gone
    {
        pump_once(&pump);
    }

    // Typed wrong once there: the phone shows a new code and asks again; the
    // right one pairs.
    pump.wrong_once = true;
    pump.requests = 0;
    net_pair_code_generate(code);
    hosts->pair = PairStatus::idle;
    check(hosts_pair(hosts, address, code), "pair starts");
    check(pump_until_pair_done(&pump) && hosts->pair == PairStatus::paired, "paired");
    check(pump.requests == 2 && pump.first_code == code && pump.last_code != pump.first_code,
          "a wrong code there: a new code here, asked again");
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
    check(hosts_pad_link(hosts).state == LinkShown::ready, "the status shows the ping");
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
    check(hosts->speed.running || hosts->known[0].measured, "the speed test at gamepad level too");
    end_wait(&pump, 20.0);
    check(!hosts->speed.running && hosts->known[0].measured, "it ends at gamepad level");
    hosts_disconnect(hosts);
    check(hosts_pad_port(hosts) == 0, "no port once left");
    check(hosts_pad_link(hosts).state == LinkShown::offline, "offline once left");

    // The host raises our level: the next connection brings it; HOME's
    // refresh (hosts_measure) measures again while connected.
    if (access->trust.devices.size() == 1)
    {
        net_access_set_level(access, 0, NetAccessLevel::full);
    }
    check(hosts_connect(hosts, 0), "connect again");
    check(pump_until_link_done(&pump) && hosts->link == LinkStatus::ready, "connected again");
    check(!hosts->known.empty() && hosts->known[0].access == NetAccessLevel::full, "the host's level");
    check(!hosts->speed.running, "measured once per run: not again on its own");
    check(hosts_measure(hosts, 0) && hosts->speed.running, "the speed test runs when asked");
    end_wait(&pump, 20.0);
    check(!hosts->speed.running, "the speed test ends");
    check(!hosts->known.empty() && hosts->known[0].down_kbps > 0 && hosts->known[0].up_kbps > 0, "speeds measured");

    // The host changes our level while we're connected: told at once.
    if (access->trust.devices.size() == 1)
    {
        net_access_set_level(access, 0, NetAccessLevel::gamepad);
    }
    check(pump_until_access(&pump, NetAccessLevel::gamepad) && hosts->link == LinkStatus::ready,
          "a lower level arrives live");
    if (access->trust.devices.size() == 1)
    {
        net_access_set_level(access, 0, NetAccessLevel::full);
    }
    check(pump_until_access(&pump, NetAccessLevel::full) && hosts->link == LinkStatus::ready,
          "a higher level arrives live");

    // REMOTE PLAY: the stream is asked for (with its tuning), a frame arrives
    // exact, it stops.
    NetStreamTuning tuning;
    tuning.delay = NetStreamDelay::automatic;
    hosts_set_stream_tuning(hosts, &tuning);
    hosts_stream_start(hosts);
    double stream_end = now_seconds() + 5.0;
    while (now_seconds() < stream_end && !pump.streaming)
    {
        pump_once(&pump);
    }
    check(pump.streaming && pump.stream_flags == (NET_STREAM_WANT_VIDEO | NET_STREAM_WANT_AUDIO),
          "stream_start reaches the host (video and audio)");
    std::vector<uint8_t> sent;
    std::vector<uint8_t> got;
    uint32_t width = 0;
    uint32_t height = 0;
    FramePixelFormat format = FramePixelFormat::xrgb8888;
    uint32_t frame = 0;
    NetStreamInfo info;
    bool exact = false;
    for (uint32_t n = 1; n <= 10 && pump.streaming; n++)
    {
        stream_picture(&sent, n);
        host_stream_frame(&pump, n, &sent);
        for (int32_t i = 0; i < 50; i++)
        {
            pump_once(&pump);
            if (hosts_take_frame(hosts, &got, &width, &height, &format, &frame, &info))
            {
                exact = frame == n && width == STREAM_WIDTH && height == STREAM_HEIGHT &&
                        format == FramePixelFormat::rgb565 && got.size() == sent.size() &&
                        std::memcmp(got.data(), sent.data(), sent.size()) == 0 &&
                        info.aspect > 1.333 && info.aspect < 1.334;   // (sent as text)
                break;
            }
        }
    }
    check(exact, "the host's frames arrive exact, with their stream_info");
    check(hosts_game_running(hosts) == 1, "a game runs (game_state)");
    NetStreamTuning applied;
    check(pump.tuning_asked == NetStreamDelay::automatic && hosts_stream_tuning_result(hosts, &applied) &&
              applied.delay_ms == 7,
          "stream_tuning asked for AUTO; the host's 7 ms read back");
    // GO HEADLESS while streaming: granted, then given back.
    check(hosts_streaming(hosts), "streaming");
    hosts_go_headless(hosts, true);
    double headless_end = now_seconds() + 5.0;
    while (now_seconds() < headless_end && !hosts->headless_on)
    {
        pump_once(&pump);
    }
    check(pump.headless && hosts->headless_on && hosts->headless_yours, "GO HEADLESS: the host's state, this phone's");
    hosts_go_headless(hosts, false);
    headless_end = now_seconds() + 5.0;
    while (now_seconds() < headless_end && hosts->headless_on)
    {
        pump_once(&pump);
    }
    check(!pump.headless && !hosts->headless_on, "GO HEADLESS: given back");
    NetClientStreamStats stream_stats;
    hosts_stream_stats(hosts, &stream_stats);
    check(stream_stats.frames >= 9 && stream_stats.audio_samples >= 9 * 64, "frames and audio counted");
    hosts_stream_stop(hosts);
    stream_end = now_seconds() + 5.0;
    while (now_seconds() < stream_end && pump.streaming)
    {
        pump_once(&pump);
    }
    check(!pump.streaming, "stream_stop reaches the host");

    // DEVICES (FULL): the host's list, a pad moved to port 3, the new list.
    NetDevice host_pad;
    host_pad.controller = 4;
    host_pad.device = 0;
    host_pad.kind = NetDeviceKind::gamepad;
    host_pad.ports = 1;
    host_pad.owner = NetDeviceOwner::host;
    host_pad.name = "8BITDO SN30 1";
    NetDevice phone = host_pad;
    phone.controller = 9;
    phone.kind = NetDeviceKind::network;
    phone.ports = 2;
    phone.owner = NetDeviceOwner::yours;
    phone.name = "TEST PHONE 1";
    pump.devices.push_back(host_pad);
    pump.devices.push_back(phone);
    hosts_request_devices(hosts);
    double devices_end = now_seconds() + 5.0;
    while (now_seconds() < devices_end && !hosts->devices_known)
    {
        pump_once(&pump);
    }
    check(hosts->devices_known && hosts->devices.size() == 2 && hosts->devices[0].name == "8BITDO SN30 1" &&
              hosts->devices[1].owner == NetDeviceOwner::yours && hosts->devices[1].ports == 2,
          "DEVICES: the host's list arrives");
    if (hosts->devices.size() == 2)
    {
        hosts_move_device(hosts, &hosts->devices[0], 2);
    }
    devices_end = now_seconds() + 5.0;
    while (now_seconds() < devices_end && !(hosts->devices.size() == 2 && hosts->devices[0].ports == 4))
    {
        pump_once(&pump);
    }
    check(hosts->devices.size() == 2 && hosts->devices[0].ports == 4, "DEVICES: a pad moved to port 3, the new list");
    hosts_disconnect(hosts);
    check(!hosts->devices_known && hosts->devices.empty(), "DEVICES: the list goes when leaving");

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

    // HOME's refresh: not connected, so it connects, measures and leaves.
    end = now_seconds() + 5.0;
    while (now_seconds() < end && !(hosts->known.size() == 1 && hosts->known[0].online))
    {
        pump_once(&pump);
    }
    check(hosts_measure(hosts, 0), "refresh starts");
    check(hosts_measuring(hosts, 0), "measuring");
    end = now_seconds() + 20.0;
    while (now_seconds() < end && hosts_measuring(hosts, 0))
    {
        pump_once(&pump);
    }
    check(!hosts_measuring(hosts, 0) && hosts->known[0].measured && hosts->known[0].down_kbps > 0, "refresh measured");
    for (int32_t i = 0; i < 20; i++)
    {
        pump_once(&pump);
    }
    check(hosts->link == LinkStatus::idle, "refresh left once done");

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
    // Given STREAM this time (the phone asks for GAMEPAD): PAIR shows and
    // keeps STREAM (Andrés, 2026-11-22: it kept GAMEPAD).
    pump.grant = NetAccessLevel::stream;
    net_pair_code_generate(code);
    check(hosts_pair(hosts, address, code) && pump_until_pair_done(&pump) && hosts->pair == PairStatus::paired,
          "paired again");
    check(hosts->known.size() == 1 && hosts->known[0].access == NetAccessLevel::stream,
          "given STREAM: the phone keeps STREAM");
    pump.grant = NetAccessLevel::gamepad;
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

    // Across the internet: a beacon here, the host registered with it; a
    // new phone waits with its code, the host's user types it in INVITE.
    BeaconServer* beacon_server = new BeaconServer{};
    BeaconNet* beacon_net = new BeaconNet{};
    BeaconServerConfig beacon_config;
    check(beacon_server_create(beacon_server, &beacon_config, beacon_now_ms()), "beacon starts");
    uint16_t beacon_port = 0;
    for (uint16_t attempt = 0; attempt < 20 && beacon_port == 0; attempt++)
    {
        uint16_t port = static_cast<uint16_t>(42000 + (beacon_now_ms() % 9000) + attempt * 2);   // its NAT check: port + 1
        if (beacon_net_open(beacon_net, "::", port, static_cast<uint16_t>(port + 1), -1))
        {
            beacon_port = port;
        }
    }
    check(beacon_port != 0, "beacon listens");
    char beacon_name[32];
    std::snprintf(beacon_name, sizeof(beacon_name), "127.0.0.1:%u", static_cast<unsigned>(beacon_port));
    NetAddress beacon_address;
    check(net_address_from_text("127.0.0.1", beacon_port, &beacon_address), "beacon address");
    net_access_set_beacon(access, &beacon_address, 1);
    pump.beacon_server = beacon_server;
    pump.beacon_net = beacon_net;

    std::string link = net_pair_link(host_identity.public_key, beacon_name);
    std::string link_beacon;
    check(hosts_link_beacon(link.c_str(), &link_beacon) && link_beacon == beacon_name &&
              !hosts_link_beacon("192.168.1.20", &link_beacon),
          "a link's beacon is read (kept for links to come)");
    std::string internet_dir = root + "/phone_internet";
    check(host_fs_make_directories(internet_dir.c_str()), "the second phone's folder");
    hosts = new Hosts{};
    pump.hosts = hosts;
    check(hosts_create(hosts, internet_dir.c_str(), "INTERNET PHONE"), "hosts_create (second phone)");
    for (int32_t i = 0; i < 100; i++)
    {
        pump_once(&pump);
    }
    check(!hosts_pair_internet(hosts, "123", beacon_name), "a code that isn't 9 digits doesn't start");
    net_pair_code_generate(code);
    check(hosts_pair_internet(hosts, code, beacon_name), "the phone waits with its code");
    check(net_access_invite(access, code), "the host's user types it in INVITE");
    end = now_seconds() + 10.0;
    while (now_seconds() < end && hosts->pair == PairStatus::busy)
    {
        pump_once(&pump);
    }
    check(hosts->pair == PairStatus::paired && hosts->known.size() == 1, "paired across the internet");
    if (hosts->known.size() == 1)
    {
        check(hosts->known[0].beacon == beacon_name, "the beacon kept");
        check(hosts->known[0].address.port == 0, "no LAN address");
        check(hosts->known[0].internet, "reachable through the beacon");
    }
    for (int32_t i = 0; i < 50; i++)
    {
        pump_once(&pump);
    }
    check(hosts_connect(hosts, 0), "connect through the beacon");
    end = now_seconds() + 10.0;
    while (now_seconds() < end && hosts->link == LinkStatus::connecting)
    {
        pump_once(&pump);
    }
    check(hosts->link == LinkStatus::ready && hosts->net.client.via_beacon, "connected across the internet");
    hosts_disconnect(hosts);

    // A host last met on the LAN that doesn't answer there (the phone left
    // home): tried at its LAN address, then through the beacon.
    if (hosts->known.size() == 1)
    {
        check(net_address_from_text("127.0.0.1", 9, &hosts->known[0].address), "a LAN address nobody answers");
        hosts->known[0].internet = false;
        hosts->known[0].online = false;
    }
    for (int32_t i = 0; i < 50; i++)
    {
        pump_once(&pump);
    }
    check(hosts_connect(hosts, 0), "connect: the LAN first");
    check(hosts->connecting_to.port == 9 || hosts->connect_pending, "tried at the LAN address");
    end = now_seconds() + 30.0;
    while (now_seconds() < end && hosts->link == LinkStatus::connecting)
    {
        pump_once(&pump);
    }
    check(hosts->lan_failed, "no answer on the LAN");
    check(hosts->link == LinkStatus::ready && hosts->net.client.via_beacon, "then connected through the beacon");
    hosts_disconnect(hosts);
    hosts_destroy(hosts);
    delete hosts;
    hosts = new Hosts{};
    check(hosts_create(hosts, internet_dir.c_str(), "INTERNET PHONE"), "hosts_create again (second phone)");
    check(hosts->known.size() == 1 && hosts->known[0].beacon == beacon_name, "the beacon saved");
    hosts_destroy(hosts);
    delete hosts;
    pump.beacon_server = nullptr;
    pump.beacon_net = nullptr;

    net_access_destroy(access);
    delete access;
    beacon_net_close(beacon_net);
    delete beacon_net;
    beacon_server_destroy(beacon_server);
    delete beacon_server;
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
