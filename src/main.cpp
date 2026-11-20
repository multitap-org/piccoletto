// piccoletto: piccolo's companion app (see CLAUDE.md). For now: the window,
// the theme and every screen with placeholder data; no networking yet.

#include <cstdlib>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "app_paths.h"
#include "app_settings.h"
#include "app_shell.h"
#include "devices_screen.h"
#include "gamepad_screen.h"
#include "haptics.h"
#include "home_screen.h"
#include "hosts.h"
#include "host_fs.h"
#include "menu_sheet.h"
#include "pad_editor.h"
#include "pad_layouts.h"
#include "pair_screen.h"
#include "piccoletto_version.h"
#include "pixel_font.h"
#include "pixel_font_imgui.h"
#include "real_gamepad.h"
#include "retropad.h"
#include "remote_play_screen.h"
#include "retro_ui.h"
#include "screens.h"
#include "settings_file.h"
#include "settings_screen.h"
#include "shader_presets.h"
#include "stream_view.h"
#include "touch_fingers.h"
#include "touch_ui.h"


struct App
{
    AppShell       shell      = {};
    TouchUi        ui         = {};
    Screen         screen     = Screen::home;
    bool           sheet_open = false;   // the ☰ sheet over GAMEPAD / REMOTE PLAY
    HomeScreen     home       = {};
    PairScreen     pair       = {};
    DevicesScreen  devices    = {};
    SettingsScreen settings_screen = {};
    PadLayouts     layouts    = {};
    PadEditor      editor     = {};
    RemoteStream   stream     = {};   // the stream's shape (placeholder until there's one)
    StreamView     stream_view = {};  // REMOTE PLAY's picture
    ShaderLibrary  shader_library = {};   // librashader, when it's there (REMOTE PLAY SHADER)
    std::string    presets_dir;           // shaders/presets next to the program
    int32_t        shader_applied = -1;   // the preset index stream_view has (-1 = none asked yet)
    std::string    shader_path;           // its file
    NetClientStreamStats stream_last = {};   // the stream's counts a second ago
    double         stream_sampled = 0.0;     // when they were taken
    PadLink        stream_rates = {};        // the last second's fps, rate, losses
    AppSettings    settings   = {};
    TouchFingers   fingers    = {};   // every finger, for the pads
    TouchFocus     focus      = {};   // the gamepad's focus this frame (SETTINGS scrolls to it)
    RetroPad       touch_pad  = {};   // the touch pad's parts held this frame (pad_layout_draw)
    bool           join_sent  = false;   // START was tapped for this connection (the pad joins a port)
    int32_t        start_frames = 0;     // frames START is still held for that tap
    Haptics        haptics    = {};   // VIBRATE ON PRESS
    RealGamepad    gamepad    = {};   // gamepads connected to the phone
    bool           touch_with_gamepad = false;   // REMOTE PLAY: touch controls shown although a gamepad is connected
    double         back_held_since = 0.0;        // the pads: BACK held since (0 = not held; -1 = used, wait for release)
    Hosts          hosts;             // the paired hosts, discovery, pairing, the connection
    PairStatus     last_pair  = PairStatus::idle;   // hosts.pair last frame (PAIR clears the code when it fails)
    std::string    settings_dir;      // settings/ ("" = nothing is saved)
    std::string    settings_path;     // settings/piccoletto.cfg ("" = nothing is saved)
    std::string    layouts_path;      // settings/layouts.cfg
    SettingsFile   settings_file;
    SettingsFile   layouts_file;
    bool           running    = false;
    bool           testing    = false;   // opened with --screen: the modes stay without a host
};

constexpr int32_t MAX_SHOWN_HOSTS = 32;   // HOME and SETTINGS list at most this many

static const KnownHost* selected_known(const App* app)
{
    int32_t index = app->home.selected;
    if (index < 0 || index >= static_cast<int32_t>(app->hosts.known.size()))
    {
        return nullptr;
    }
    return &app->hosts.known[static_cast<size_t>(index)];
}

static const char* selected_host(const App* app)
{
    const KnownHost* host = selected_known(app);
    return host != nullptr ? host->name.c_str() : "";
}

// Full access: the host granted this phone everything (the HOTKEY button).
static bool selected_full_access(const App* app)
{
    const KnownHost* host = selected_known(app);
    return host != nullptr && host->access == NetAccessLevel::full;
}

// HOME's view of the paired hosts: the modes each access level opens.
static int32_t home_hosts(const App* app, HomeHost* out)
{
    int32_t count = 0;
    for (size_t i = 0; i < app->hosts.known.size() && count < MAX_SHOWN_HOSTS; i++)
    {
        const KnownHost* host = &app->hosts.known[i];
        HomeHost* shown = &out[count];
        *shown = HomeHost{};
        shown->name = host->name.c_str();
        shown->online = host->online;
        shown->access.gamepad = true;
        shown->access.remote_play = host->access != NetAccessLevel::gamepad;
        shown->access.devices = host->access == NetAccessLevel::full;
        shown->down_kbps = host->down_kbps;
        shown->up_kbps = host->up_kbps;
        shown->can_measure = host->online && host->access != NetAccessLevel::gamepad;
        shown->measuring = hosts_measuring(&app->hosts, static_cast<int32_t>(i));
        count++;
    }
    return count;
}

// piccoletto's folder, like piccolo's (frontends-common's app_paths.h):
// $XDG_DATA_HOME/piccoletto or ~/.local/share/piccoletto; where SDL keeps an
// app's files elsewhere (Android). Its settings/ folder is made here.
static void find_files(App* app)
{
    std::string root;
    bool found = false;
#if !defined(SDL_PLATFORM_ANDROID)
    // (Android apps may have a HOME that isn't theirs: only SDL's folder there.)
    found = app_paths_xdg_root(std::getenv("XDG_DATA_HOME"), std::getenv("HOME"), "piccoletto", &root);
#endif
    if (!found)
    {
        char* pref = SDL_GetPrefPath("multitap", "piccoletto");
        if (pref == nullptr)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No folder for piccoletto's settings: nothing will be saved");
            return;
        }
        root = pref;
        SDL_free(pref);
    }
    std::string settings = root + "/settings";
    if (!host_fs_make_directories(settings.c_str()))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Can't create %s: nothing will be saved", settings.c_str());
        return;
    }
    app->settings_dir = settings;
    app->settings_path = settings + "/piccoletto.cfg";
    app->layouts_path = settings + "/layouts.cfg";
}

static void save_settings(App* app)
{
    if (app->settings_path.empty())
    {
        return;
    }
    app_settings_save(&app->settings, &app->settings_file);
    settings_file_save(&app->settings_file, app->settings_path.c_str(), "piccoletto's settings");
}

static void save_layouts(App* app)
{
    if (app->layouts_path.empty())
    {
        return;
    }
    for (int32_t i = 0; i < PAD_LAYOUT_COUNT; i++)
    {
        pad_layout_save(&app->layouts.layouts[i], &app->layouts_file);
    }
    settings_file_save(&app->layouts_file, app->layouts_path.c_str(), "piccoletto's pad layouts (EDIT LAYOUT)");
}

static void load_files(App* app)
{
    if (!app->settings_path.empty())
    {
        settings_file_load(&app->settings_file, app->settings_path.c_str());
    }
    app_settings_load(&app->settings, &app->settings_file);
    if (!app->layouts_path.empty())
    {
        settings_file_load(&app->layouts_file, app->layouts_path.c_str());
    }
    for (int32_t i = 0; i < PAD_LAYOUT_COUNT; i++)
    {
        pad_layout_load(&app->layouts.layouts[i], &app->layouts_file);
    }
}

// What follows the settings every frame: the theme, and the overlays' opacity
// where EDIT LAYOUT didn't set one.
static void apply_settings(App* app)
{
    app->ui.palette = *retro_ui_theme_palette(app->settings.theme);
    // The gamepad's focus (ImGui draws it around the button) in the theme's highlight.
    uint32_t focus = app->ui.palette.highlight;
    ImGui::GetStyle().Colors[ImGuiCol_NavCursor] =
        ImVec4(static_cast<float>((focus >> 16) & 0xFFu) / 255.0f, static_cast<float>((focus >> 8) & 0xFFu) / 255.0f,
               static_cast<float>(focus & 0xFFu) / 255.0f, 1.0f);
    PadLayoutId overlays[2] = {PadLayoutId::remote_overlay_portrait, PadLayoutId::remote_overlay_landscape};
    for (int32_t i = 0; i < 2; i++)
    {
        PadLayout* layout = pad_layouts_get(&app->layouts, overlays[i]);
        layout->default_alpha = app->settings.overlay_opacity;
        if (!layout->alpha_set)
        {
            layout->alpha = app->settings.overlay_opacity;
        }
    }
}

// Turns the screen. On the desktop it swaps the window's width and height.
// On a phone the screen follows the phone by itself (unless the system's
// rotation lock is on: SDL asks Android for "full user"); ROTATE locks it to
// the other side, whatever the sensor and the system lock say, until the next
// ROTATE. SDL only passes SDL_HINT_ORIENTATIONS on to Android when the
// window's resizable flag changes, hence the toggle.
static void rotate(App* app)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(app->shell.window, &width, &height);
#if defined(SDL_PLATFORM_ANDROID)
    const char* other = (width > height) ? "Portrait" : "LandscapeLeft LandscapeRight";
    SDL_SetHint(SDL_HINT_ORIENTATIONS, other);
    SDL_SetWindowResizable(app->shell.window, false);
    SDL_SetWindowResizable(app->shell.window, true);
#else
    SDL_SetWindowSize(app->shell.window, height, width);
#endif
}

static void go_to(App* app, Screen screen)
{
    if (app->screen == Screen::settings && screen != Screen::settings)
    {
        save_settings(app);
        hosts_set_name(&app->hosts, app->settings.phone_name);
    }
    if (app->screen == Screen::pair && screen != Screen::pair)
    {
        hosts_cancel(&app->hosts);   // leaving PAIR stops a pairing that's still running
    }
    // The modes talk to the selected host: connected while one is open.
    bool mode = screen == Screen::gamepad || screen == Screen::remote_play || screen == Screen::devices;
    if (app->screen == Screen::remote_play && screen != Screen::remote_play)
    {
        hosts_stream_stop(&app->hosts);   // the host stops sending; the next stream starts black
        stream_view_clear(&app->stream_view);
    }
    if (mode)
    {
        hosts_connect(&app->hosts, app->home.selected);
    }
    else if (screen != app->screen)
    {
        hosts_disconnect(&app->hosts);
    }
    if (screen == Screen::remote_play && app->screen != Screen::remote_play)
    {
        hosts_stream_start(&app->hosts);   // now, or as soon as the connection is ready
    }
    if (app->editor.active)
    {
        // Leaving while editing keeps what was done so far.
        app->editor.active = false;
        save_layouts(app);
    }
    if (screen == Screen::pair)
    {
        pair_screen_reset(&app->pair);
    }
    app->screen = screen;
    app->sheet_open = false;
}

// What a tap on a screen (or the sheet) asked for.
static void handle_action(App* app, NavAction action)
{
    switch (action)
    {
        case NavAction::none:
            break;
        case NavAction::home:
            go_to(app, Screen::home);
            break;
        case NavAction::menu:
            if (app->screen == Screen::gamepad || app->screen == Screen::remote_play)
            {
                app->sheet_open = !app->sheet_open;
            }
            else if (app->screen == Screen::home)
            {
                go_to(app, Screen::settings);
            }
            break;
        case NavAction::rotate:
            app->sheet_open = false;
            rotate(app);
            break;
        case NavAction::pair:
            go_to(app, Screen::pair);
            break;
        case NavAction::gamepad:
            go_to(app, Screen::gamepad);
            break;
        case NavAction::remote_play:
            go_to(app, Screen::remote_play);
            break;
        case NavAction::devices:
            go_to(app, Screen::devices);
            break;
        case NavAction::edit_layout:
            app->sheet_open = false;
            app->editor.active = true;
            break;
        case NavAction::edit_done:
            app->editor.active = false;
            save_layouts(app);
            break;
        case NavAction::settings:
            go_to(app, Screen::settings);
            break;
        case NavAction::measure:
            hosts_measure(&app->hosts, app->home.selected);
            break;
        case NavAction::touch_controls:
            app->touch_with_gamepad = !app->touch_with_gamepad;
            app->sheet_open = false;
            break;
    }
}

// The back button (Android's, Escape on the desktop): closes the sheet or
// EDIT LAYOUT or HOME's sidebar, else goes back to HOME, else leaves the app.
static void go_back(App* app)
{
    if (app->sheet_open)
    {
        app->sheet_open = false;
    }
    else if (app->screen == Screen::home && app->home.sidebar_open)
    {
        app->home.sidebar_open = false;
    }
    else if (app->editor.active)
    {
        handle_action(app, NavAction::edit_done);
    }
    else if (app->screen != Screen::home)
    {
        go_to(app, Screen::home);
    }
    else
    {
        app->running = false;
    }
}

static void handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        bool typing = ImGui::GetIO().WantTextInput;
        app_shell_process_event(&app->shell, &event);
        const ImGuiIO& io = ImGui::GetIO();
        touch_fingers_event(&app->fingers, &event, io.DisplaySize.x, io.DisplaySize.y);
        real_gamepad_event(&app->gamepad, &event);
        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        // Escape in a text field is the field's (it stops editing).
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.key == SDLK_AC_BACK || (event.key.key == SDLK_ESCAPE && !typing)))
        {
            go_back(app);
        }
    }
}

// The real gamepad outside play (Andrés: the SHIELD's touch can't be used):
// ImGui's gamepad navigation on every screen but the pads (d-pad / stick
// move the focus, A presses, B goes back); on GAMEPAD and REMOTE PLAY the
// gamepad plays, and holding BACK for a second goes to HOME.
constexpr double BACK_HOLD_SECONDS = 1.0;

static void update_gamepad_ui(App* app, double now)
{
    RealGamepad* pads = &app->gamepad;
    if (pads->changed)
    {
        // Only ours (devices that aren't gamepads left out) drive ImGui.
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, pads->open, pads->count);
        pads->changed = false;
    }
    bool on_pad = (app->screen == Screen::gamepad || app->screen == Screen::remote_play) && !app->sheet_open &&
                  !app->editor.active;
    ImGuiIO& io = ImGui::GetIO();
    if (on_pad)
    {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        bool back = real_gamepad_held(pads, SDL_GAMEPAD_BUTTON_BACK);
        if (!back)
        {
            app->back_held_since = 0.0;
        }
        else if (app->back_held_since == 0.0)
        {
            app->back_held_since = now;
        }
        else if (app->back_held_since > 0.0 && now - app->back_held_since >= BACK_HOLD_SECONDS)
        {
            app->back_held_since = -1.0;   // until it's let go
            SDL_Log("BACK held: HOME");
            go_to(app, Screen::home);
        }
        return;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    if (app->back_held_since < 0.0 && real_gamepad_held(pads, SDL_GAMEPAD_BUTTON_BACK))
    {
        return;   // the BACK that just left a pad: not a press here
    }
    app->back_held_since = 0.0;
    if (real_gamepad_pressed(pads, SDL_GAMEPAD_BUTTON_EAST) || real_gamepad_pressed(pads, SDL_GAMEPAD_BUTTON_BACK))
    {
        go_back(app);
    }
}

// This phone's pad to the connected host, once a frame: the touch pad's
// parts held and the real gamepads, on GAMEPAD / REMOTE PLAY while they're
// played (not under the sheet or in EDIT LAYOUT: then nothing is held, so
// what was held is let go).
constexpr int32_t JOIN_START_FRAMES = 6;   // about 100 ms: a tap the host can't miss

static void send_pad(App* app)
{
    bool playing = (app->screen == Screen::gamepad || app->screen == Screen::remote_play) && !app->sheet_open &&
                   !app->editor.active;
    RetroPad pad;
    if (playing)
    {
        pad = app->touch_pad;
        RetroPad gamepad;
        real_gamepad_retropad(&app->gamepad, &gamepad);
        retropad_merge(&pad, &gamepad);
    }
    // Joining at once (Andrés): when the host says our pad isn't on a port,
    // START is tapped for it (START joins on piccolo). Once per connection;
    // leaving (HOME) closes it and the host unplugs the pad.
    const Hosts* hosts = &app->hosts;
    if (hosts->link != LinkStatus::ready)
    {
        app->join_sent = false;
        app->start_frames = 0;
    }
    else if (playing && hosts->controllers_known && hosts->pad_port < 0 && !app->join_sent)
    {
        app->join_sent = true;
        app->start_frames = JOIN_START_FRAMES;
        SDL_Log("Network: tapping START to join a port");
    }
    if (app->start_frames > 0)
    {
        retropad_press(&pad, RetroPadButton::start);
        app->start_frames--;
    }
    hosts_send_pad(&app->hosts, &pad);
}

// PAIR: the unpaired hosts found, the code, how pairing goes. When it's
// done, HOME with the new host on the card.
static NavAction draw_pair(App* app)
{
    Hosts* hosts = &app->hosts;
    const char* found[HOSTS_MAX_FOUND];
    int32_t found_count = hosts_unpaired_count(hosts);
    for (int32_t i = 0; i < found_count; i++)
    {
        found[i] = hosts_unpaired(hosts, i)->name;
    }
    PairProgress progress;
    if (!hosts->ok)
    {
        progress.text = "NO NETWORK";
        progress.error = true;
    }
    else if (hosts->pair == PairStatus::busy)
    {
        progress.busy = true;
        progress.text = hosts_pair_connecting(hosts) ? "CONNECTING…" : "CHECKING THE CODE…";
    }
    else if (hosts->pair == PairStatus::failed)
    {
        progress.text = hosts_pair_failure_text(hosts);
        progress.error = true;
    }
    bool done = false;
    NavAction action = pair_screen_draw(&app->pair, &app->ui, found, found_count, &progress, &done);
    // A failed attempt: the host has a new code by now, so the typed one goes;
    // the next key clears the message.
    if (hosts->pair == PairStatus::failed && app->last_pair != PairStatus::failed)
    {
        pair_screen_reset(&app->pair);
    }
    else if (hosts->pair == PairStatus::failed && app->pair.typed)
    {
        hosts->pair = PairStatus::idle;
    }
    app->last_pair = hosts->pair;
    if (done)
    {
        NetAddress address;
        bool have = false;
        if (app->pair.typing_address)
        {
            have = hosts_parse_address(app->pair.address, &address);
        }
        else if (app->pair.selected < found_count)
        {
            address = hosts_unpaired(hosts, app->pair.selected)->address;
            have = true;
        }
        if (!have || !hosts_pair(hosts, address, app->pair.code))
        {
            hosts->pair = PairStatus::failed;
            hosts->refusal = NetClientRefusal::connection;
        }
    }
    if (hosts->pair == PairStatus::paired)
    {
        app->home.selected = hosts->paired_index;
        hosts->pair = PairStatus::idle;
        action = NavAction::home;
    }
    return action;
}

static void draw_screen(App* app)
{
    NavAction action = NavAction::none;
    int32_t forget = -1;
    // The pads take fingers only while nothing covers them.
    bool covered = app->sheet_open || (app->screen == Screen::home && app->home.sidebar_open);
    app->ui.fingers = covered ? nullptr : &app->fingers;
    app->ui.gamepad = &app->gamepad;
    app->focus = TouchFocus{};
    app->ui.focus = &app->focus;
    app->touch_pad = RetroPad{};
    app->ui.pad = &app->touch_pad;
    // REMOTE PLAY: the touch controls hide while a real gamepad is connected (unless shown from the sheet).
    bool gamepad_connected = real_gamepad_connected(&app->gamepad);
    bool show_touch = !gamepad_connected || app->touch_with_gamepad || app->editor.active;
    HomeHost shown[MAX_SHOWN_HOSTS];
    int32_t shown_count = home_hosts(app, shown);
    PadLink link = hosts_pad_link(&app->hosts);
    if (app->screen == Screen::remote_play)
    {
        link.streaming = app->stream_rates.streaming;
        link.fps = app->stream_rates.fps;
        link.kbps = app->stream_rates.kbps;
        link.lost = app->stream_rates.lost;
        link.audio_gaps = app->stream_rates.audio_gaps;
        NetStreamTuning tuning;
        if (hosts_stream_tuning_result(&app->hosts, &tuning))
        {
            link.delay_ms = tuning.delay_ms;
        }
    }
    touch_ui_begin_screen(&app->ui, "##screen");
    switch (app->screen)
    {
        case Screen::home:
            action = home_screen_draw(&app->home, &app->ui, shown, shown_count, hosts_unpaired_count(&app->hosts));
            break;
        case Screen::pair:
            action = draw_pair(app);
            break;
        case Screen::gamepad:
            action = gamepad_screen_draw(&app->ui, selected_host(app), hosts_pad_port(&app->hosts), &link,
                                         selected_full_access(app), &app->layouts, &app->editor);
            break;
        case Screen::remote_play:
            action = remote_play_screen_draw(&app->ui, selected_host(app), hosts_pad_port(&app->hosts), &link,
                                             selected_full_access(app), show_touch, &app->stream, &app->layouts,
                                             &app->editor);
            break;
        case Screen::devices:
            action = devices_screen_draw(&app->devices, &app->ui, selected_host(app));
            break;
        case Screen::settings:
        {
            const char* names[MAX_SHOWN_HOSTS];
            for (int32_t i = 0; i < shown_count; i++)
            {
                names[i] = shown[i].name;
            }
            action = settings_screen_draw(&app->settings_screen, &app->ui, &app->settings, PICCOLETTO_VERSION, names, shown_count,
                                          &forget);
            if (forget >= 0)
            {
                SDL_Log("FORGET: %s", names[forget]);
                hosts_forget(&app->hosts, forget);
                app->home.selected = 0;
            }
            break;
        }
    }
    touch_ui_end_screen();
    // HOME's sidebar (landscape) the same way, above HOME.
    if (app->screen == Screen::home && app->home.sidebar_open)
    {
        touch_ui_begin_overlay(&app->ui, "##sidebar_window");
        home_sidebar_draw(&app->home, &app->ui, shown, shown_count);
        touch_ui_end_screen();
    }
    // The sheet in its own window above the screen: the screen behind takes no taps.
    if (app->sheet_open)
    {
        touch_ui_begin_overlay(&app->ui, "##sheet_window");
        action = menu_sheet_draw(&app->ui, app->screen, app->screen == Screen::remote_play && gamepad_connected, show_touch);
        touch_ui_end_screen();
    }
    if (action != NavAction::none)
    {
        SDL_Log("%s on %s", nav_action_name(action), selected_host(app));
    }
    handle_action(app, action);
}

// Desktop testing: --screen <name> opens on that screen, --landscape starts
// with the window turned, --aspect W:H sets the stream's shape (4:3, 8:7,
// 2:3 for the DS...), --edit opens GAMEPAD / REMOTE PLAY in EDIT LAYOUT.
static bool parse_screen(const char* name, Screen* screen)
{
    static const char* const NAMES[] = {"home", "pair", "gamepad", "remote_play", "devices", "settings"};
    static const Screen SCREENS[] = {Screen::home, Screen::pair, Screen::gamepad, Screen::remote_play, Screen::devices,
                                     Screen::settings};
    for (int32_t i = 0; i < 6; i++)
    {
        if (SDL_strcmp(name, NAMES[i]) == 0)
        {
            *screen = SCREENS[i];
            return true;
        }
    }
    return false;
}

// REMOTE PLAY: the newest picture uploaded; the stream's shape, or why
// there's no picture.
// REMOTE PLAY's pacing (SETTINGS), applied only while it's open: the
// picture queue, frames in flight, whether the next frame starts late. Not
// saved anywhere; leaving puts the defaults back.
static void apply_pacing(App* app)
{
    bool playing = app->screen == Screen::remote_play;
    StreamPacing pacing = playing ? app->settings.pacing : StreamPacing::balanced;
    uint32_t queue = pacing == StreamPacing::smooth ? 3 : 1;
    uint32_t in_flight = pacing == StreamPacing::latency ? 1 : FRAMES_IN_FLIGHT;
    bool start_late = pacing == StreamPacing::latency;
    hosts_set_frame_queue(&app->hosts, queue);
    // And on the host, for this stream only (stream_tuning; piccolo's
    // settings aren't touched): SMOOTH keeps the host's own frame delay,
    // BALANCED and LATENCY ask for its AUTO search (run-ahead later).
    if (playing)
    {
        NetStreamTuning tuning;
        tuning.delay = pacing == StreamPacing::smooth ? NetStreamDelay::host : NetStreamDelay::automatic;
        hosts_set_stream_tuning(&app->hosts, &tuning);
    }
    Renderer* renderer = &app->shell.renderer;
    if (renderer->frames_in_flight != in_flight || renderer->wait_for_present != start_late)
    {
        renderer_set_frames_in_flight(renderer, &app->shell.vulkan, in_flight);
        renderer->wait_for_present = start_late;
        SDL_Log("REMOTE PLAY pacing: %s", pacing == StreamPacing::latency ? "latency" :
                                         (pacing == StreamPacing::smooth ? "smooth" : "balanced"));
    }
}

static void update_stream(App* app)
{
    RemoteStream* stream = &app->stream;
    stream->view = &app->stream_view;
    stream->shell = &app->shell;
    apply_pacing(app);
    if (app->screen != Screen::remote_play)
    {
        return;
    }
    StreamView* view = &app->stream_view;
    // The shader preset: loaded only when the choice changes (it waits for the GPU).
    if (app->settings.shader != app->shader_applied)
    {
        app->shader_applied = app->settings.shader;
        const ShaderPreset* preset = shader_preset(app->settings.shader);
        app->shader_path = preset->key[0] != '\0' ? app->presets_dir + "/" + preset->key : std::string();
        stream_view_set_shader(view, &app->shader_library, &app->shell, app->shader_path.c_str());
    }
    stream_view_update(view, &app->hosts, &app->shell);
    // The last second's rates, for the status line.
    double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
    if (now - app->stream_sampled >= 1.0)
    {
        NetClientStreamStats stats;
        hosts_stream_stats(&app->hosts, &stats);
        double seconds = app->stream_sampled > 0.0 ? now - app->stream_sampled : 1.0;
        bool continued = stats.frames >= app->stream_last.frames;   // not restarted since
        PadLink* rates = &app->stream_rates;
        rates->streaming = app->hosts.link == LinkStatus::ready && continued;
        rates->fps = continued ? static_cast<uint32_t>((stats.frames - app->stream_last.frames) / seconds + 0.5) : 0;
        rates->kbps = continued ? static_cast<uint32_t>(static_cast<double>(stats.bytes - app->stream_last.bytes) * 8.0 /
                                                        seconds / 1000.0)
                                : 0;
        rates->lost = continued ? stats.lost - app->stream_last.lost : 0;
        rates->audio_gaps = continued && stats.audio_gaps >= app->stream_last.audio_gaps
                                ? stats.audio_gaps - app->stream_last.audio_gaps
                                : 0;
        app->stream_last = stats;
        app->stream_sampled = now;
    }
    if (view->has_picture && view->width > 0 && view->height > 0)
    {
        stream->width = static_cast<int32_t>(view->width);
        stream->height = static_cast<int32_t>(view->height);
        double aspect = view->info.aspect > 0.0 ? view->info.aspect
                                                : static_cast<double>(view->width) / static_cast<double>(view->height);
        stream->aspect = static_cast<float>(aspect);
        SDL_snprintf(stream->label, sizeof(stream->label), "%.2f", aspect);
        stream->aspect_label = stream->label;
        stream->status = nullptr;
        return;
    }
    if (app->testing && app->hosts.link != LinkStatus::ready)
    {
        stream->status = nullptr;   // --screen without a host: the placeholder
    }
    else if (app->hosts.link != LinkStatus::ready)
    {
        stream->status = "CONNECTING…";
    }
    else if (hosts_game_running(&app->hosts) == 0)
    {
        stream->status = "NO GAME RUNNING";
    }
    else
    {
        stream->status = "WAITING FOR THE PICTURE…";
    }
}

static bool parse_aspect(const char* text, RemoteStream* stream)
{
    int width = 0;
    int height = 0;
    if (SDL_sscanf(text, "%d:%d", &width, &height) != 2 || width <= 0 || height <= 0)
    {
        return false;
    }
    stream->aspect = static_cast<float>(width) / static_cast<float>(height);
    stream->aspect_label = text;
    // The label's picture size: SNES-like, or the DS's two screens for 2:3.
    if (width == 2 && height == 3)
    {
        stream->width = 256;
        stream->height = 384;
    }
    return true;
}

int main(int argc, char** argv)
{
    SDL_Log("piccoletto %s", PICCOLETTO_VERSION);

    App* app = new App{};
    devices_screen_init(&app->devices);
    pad_layouts_init(&app->layouts);

    Screen first = Screen::home;
    bool landscape = false;
    bool edit = false;
    for (int i = 1; i < argc; i++)
    {
        if (SDL_strcmp(argv[i], "--landscape") == 0)
        {
            landscape = true;
        }
        else if (SDL_strcmp(argv[i], "--edit") == 0)
        {
            edit = true;
        }
        else if (SDL_strcmp(argv[i], "--screen") == 0 && i + 1 < argc && parse_screen(argv[i + 1], &first))
        {
            app->testing = true;
            i++;
        }
        else if (SDL_strcmp(argv[i], "--aspect") == 0 && i + 1 < argc && parse_aspect(argv[i + 1], &app->stream))
        {
            i++;
        }
        else
        {
            SDL_Log("Usage: piccoletto [--screen home|pair|gamepad|remote_play|devices|settings] [--landscape] [--aspect W:H] [--edit]");
        }
    }

    // A phone-shaped window on the desktop (2 window pixels per dp).
    AppShellConfig config = {};
    config.name          = "piccoletto";
    config.version       = PICCOLETTO_VERSION;
    config.identifier    = "site.multitap.piccoletto";
    config.window_width  = landscape ? 900 : 420;
    config.window_height = landscape ? 420 : 900;

    // Devices SDL would take for gamepads that aren't: Android TV's
    // "virtual-search" (the remote's search key; Google 18d1:0100), on the SHIELD.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, "0x18D1/0x0100");

    if (!pixel_font_init())
    {
        delete app;
        return 1;
    }
    if (!app_shell_create(&app->shell, &config))
    {
        app_shell_destroy(&app->shell);
        delete app;
        return 1;
    }
    app->ui.font = pixel_font_imgui_add();
    if (app->ui.font == nullptr)
    {
        app_shell_destroy(&app->shell);
        delete app;
        return 1;
    }
    ImGui::GetIO().IniFilename = nullptr;   // no window layout to remember: every screen is full screen
    // Gamepads for navigation are handed over by hand (update_gamepad_ui), none until then.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
#if defined(SDL_PLATFORM_ANDROID)
    // Full screen on a phone: SDL's activity hides the status and navigation
    // bars (immersive) only for a fullscreen window.
    if (!SDL_SetWindowFullscreen(app->shell.window, true))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetWindowFullscreen failed: %s", SDL_GetError());
    }
#endif
    find_files(app);
    load_files(app);
    if (!app->settings_dir.empty())
    {
        hosts_create(&app->hosts, app->settings_dir.c_str(), app->settings.phone_name);
    }
    haptics_open(&app->haptics);
    // librashader (REMOTE PLAY SHADER): next to the program, or where the
    // system finds it (Android: the APK's lib folder). Without it, no shaders.
    {
        std::vector<std::string> folders;
        const char* base = SDL_GetBasePath();
        if (base != nullptr)
        {
            folders.push_back(base);
            app->presets_dir = std::string(base) + "shaders/presets";
        }
        shader_library_load(&app->shader_library, folders);
#if defined(SDL_PLATFORM_ANDROID)
        // The presets live in the APK; librashader reads plain files.
        if (!app->settings_dir.empty())
        {
            std::string shaders = app->settings_dir + "/../shaders";
            shader_presets_unpack(shaders.c_str(), PICCOLETTO_VERSION);
            app->presets_dir = shaders + "/presets";
        }
#endif
    }

    go_to(app, first);
    app->editor.active = edit && (first == Screen::gamepad || first == Screen::remote_play);
    app->running = true;
    while (app->running)
    {
        handle_events(app);
        if (!app_shell_begin_frame(&app->shell))
        {
            continue;
        }
        const ImGuiIO& io = ImGui::GetIO();
        app->ui.scale = touch_ui_scale_for(io.DisplaySize.x, io.DisplaySize.y);
        apply_settings(app);
        real_gamepad_update(&app->gamepad);
        double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
        update_gamepad_ui(app, now);
        hosts_update(&app->hosts, now);
        // The modes need the host (Andrés): when the connection drops, or
        // can't be made, back to HOME.
        bool in_mode = app->screen == Screen::gamepad || app->screen == Screen::remote_play ||
                       app->screen == Screen::devices;
        if (in_mode && app->hosts.link == LinkStatus::failed && !app->testing)
        {
            SDL_Log("Network: lost %s (%s): HOME", selected_host(app), hosts_refusal_text(app->hosts.refusal));
            go_to(app, Screen::home);
        }
        update_stream(app);
        draw_screen(app);
        stream_view_record(&app->stream_view, &app->shell);
        send_pad(app);
        if (app->fingers.presses > 0 && app->settings.vibrate)
        {
            haptics_buzz(&app->haptics);
        }
        touch_fingers_end_frame(&app->fingers);

        uint32_t background = app->ui.palette.background;
        ClearColor clear = {static_cast<float>((background >> 16) & 0xFFu) / 255.0f,
                            static_cast<float>((background >> 8) & 0xFFu) / 255.0f,
                            static_cast<float>(background & 0xFFu) / 255.0f, 1.0f};
        if (!app_shell_present(&app->shell, clear))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
            break;
        }
    }

    go_to(app, Screen::home);   // saves what the current screen holds (settings, a layout being edited)
    haptics_close(&app->haptics);
    hosts_destroy(&app->hosts);
    stream_view_destroy(&app->stream_view, &app->shell);
    shader_library_unload(&app->shader_library);
    real_gamepad_close(&app->gamepad);
    app_shell_destroy(&app->shell);
    delete app;
    return 0;
}
