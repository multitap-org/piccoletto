// piccoletto: piccolo's companion app. For now: the window,
// the theme and every screen with placeholder data; no networking yet.

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#if defined(SDL_PLATFORM_EMSCRIPTEN)
#include <emscripten/emscripten.h>
#endif
#if defined(SDL_PLATFORM_ANDROID)
#include <jni.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "app_loop.h"
#include "app_paths.h"
#include "app_settings.h"
#include "app_assets.h"
#include "app_icon.h"
#include "app_shell.h"
#include "app_storage.h"
#include "ports_overlay.h"
#include "gamepad_screen.h"
#include "haptics.h"
#include "input_hold.h"
#include "home_screen.h"
#include "hosts.h"
#include "host_fs.h"
#include "quick_menu.h"
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
#include "shader_check.h"
#include "shader_check_view.h"
#include "shader_presets.h"
#include "stream_view.h"
#include "touch_fingers.h"
#include "touch_ui.h"


struct App
{
    AppShell       shell      = {};
    TouchUi        ui         = {};
    Screen         screen     = Screen::home;
    bool           menu_open = false;   // the quick menu over GAMEPAD / REMOTE PLAY
    QuickMenu      quick_menu = {};
    InputHold      start_hold = {};   // the pads: holding START opens the quick menu
    bool           devices_open = false; // DEVICES over GAMEPAD / REMOTE PLAY (FULL access)
    HomeScreen     home       = {};
    PairScreen     pair       = {};
    PortsOverlay   ports      = {};   // the ports' tabs over the pads, and DEVICES
    std::vector<NetDevice> example_devices;   // --screen devices without a host
    SettingsScreen settings_screen = {};
    PadLayouts     layouts    = {};
    PadEditor      editor     = {};
    RemoteStream   stream     = {};   // the stream's shape (placeholder until there's one)
    StreamView     stream_view = {};  // REMOTE PLAY's picture
    ShaderLibrary  shader_library = {};   // librashader, when it's there (REMOTE PLAY SHADER)
    std::string    presets_dir;           // the GPU path's presets: shaders/presets (Vulkan) or shaders/gl
    int32_t        shader_applied = -1;   // the preset index stream_view has (-1 = none asked yet)
    // A preset just applied that's too slow for this phone asks KEEP / REVERT
    // (frontends-common's shader_check, retroui's sheet). REVERT goes back to
    // the last preset that was fine (`shader_kept`: the one before the first
    // of a run of changes).
    ShaderCheck    shader_check = {};
    uint64_t       shader_check_asked_ms = 0;
    int32_t        shader_kept = 0;
    bool           shader_reverted = false;   // the next change is REVERT's: not checked again
    std::string    shader_path;           // its file
    NetClientStreamStats stream_last = {};   // the stream's counts a second ago
    double         stream_sampled = 0.0;     // when they were taken
    PadLink        stream_rates = {};        // the last second's fps, rate, losses
    AppSettings    settings   = {};
    TouchFingers   fingers    = {};   // every finger, for the pads
    TouchFocus     focus      = {};   // the gamepad's focus this frame (SETTINGS scrolls to it)
    RetroPad       touch_pad  = {};   // the touch pad's parts held this frame (pad_layout_draw)
    RetroPad       gamepad_pad = {};  // what the real gamepads hold, as a RetroPad (lights the pads' parts)
    bool           join_sent  = false;   // START was tapped for this connection (the pad joins a port)
    int32_t        start_frames = 0;     // frames START is still held for that tap
    Haptics        haptics    = {};   // VIBRATE ON PRESS
    RealGamepad    gamepad    = {};   // gamepads connected to the phone
    bool           touch_with_gamepad = false;   // REMOTE PLAY: touch controls shown although a gamepad is connected
    InputHold      back_hold = {};              // the pads: holding BACK goes to HOME
    Hosts          hosts;             // the paired hosts, discovery, pairing, the connection
    PairStatus     last_pair  = PairStatus::idle;   // hosts.pair last frame (a failure makes a new code)
    PairStep       pair_step  = PairStep::pick;     // PAIR's step (waiting follows hosts.pair)
    bool           pair_internet = false;           // PAIR is waiting across the internet
    int32_t        pair_host  = -1;                 // PAIRED: the new host (index in hosts.known)
    std::string    pair_beacon;                     // INTERNET goes through it ("" = the default; a link's)
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

// What the selected host lets this phone do: STREAM and up remote play and
// the HOTKEY button; FULL and up DEVICES.
static bool selected_allows(const App* app, NetAccessLevel level)
{
    const KnownHost* host = selected_known(app);
    return host != nullptr && net_access_allows(host->access, level);
}

static bool selected_full_access(const App* app)
{
    return selected_allows(app, NetAccessLevel::full);
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
        // Not on the LAN now: met across the internet through its beacon
        // (every paired host has one: the default beacon for LAN pairings).
        shown->internet = !host->online;
        shown->access.gamepad = true;
        shown->access.remote_play = net_access_allows(host->access, NetAccessLevel::stream);
        shown->down_kbps = host->down_kbps;
        shown->up_kbps = host->up_kbps;
        shown->can_measure = host->online || shown->internet;
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
    pad_layouts_save_file(&app->layouts, &app->layouts_file);
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
    pad_layouts_load_file(&app->layouts, &app->layouts_file);
}

// What follows the settings every frame: the theme, and the overlays' opacity
// where EDIT LAYOUT didn't set one.
static void apply_settings(App* app)
{
    app->ui.palette = *retro_ui_theme_palette(app->settings.theme);
    // The gamepad's focus (ImGui draws it around the button) in the theme's highlight.
    uint32_t focus = app->ui.palette.highlight;
    ImGui::GetStyle().Colors[ImGuiCol_NavCursor] = retro_ui_color_vec4(focus | 0xFF000000u);
    PadLayoutId overlays[2] = {PadLayoutId::remote_overlay_portrait, PadLayoutId::remote_overlay_landscape};
    for (int32_t i = 0; i < 2; i++)
    {
        pad_layout_set_default_alpha(pad_layouts_get(&app->layouts, overlays[i]), app->settings.overlay_opacity);
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
#elif defined(SDL_PLATFORM_EMSCRIPTEN)
    // The browser: the page locks the screen's orientation (phones, and only
    // in full screen; elsewhere the browser says no and nothing turns).
    (void)app;
    emscripten_run_script(width > height ? "piccoletto_page_rotate('portrait')" : "piccoletto_page_rotate('landscape')");
#else
    SDL_SetWindowSize(app->shell.window, height, width);
#endif
}

// The quick menu: SHADER / PACING picked in it are saved when it closes.
// The host's game is paused (GAMEPAD / REMOTE PLAY): a strip at the top
// says so and why, since the phone's START only reaches the game, never
// the host's menus (Andrés, 2026-11-21). USER alone: RESUME in the quick
// menu lifts it; the others only the host lifts. Drawn in the screen, not
// a window: the pads under it keep their taps.
static void draw_pause_banner(App* app)
{
    uint32_t causes = 0;
    if (!hosts_pause(&app->hosts, &causes) || causes == 0)
    {
        return;
    }
    const char* why = "";
    if ((causes & NET_PAUSE_MENU) != 0)
    {
        why = "MENU";
    }
    else if ((causes & NET_PAUSE_FOCUS) != 0)
    {
        why = "FOCUS LOST";
    }
    else if ((causes & NET_PAUSE_CONTROLLERS) != 0)
    {
        why = "NO CONTROLLERS";
    }
    else if ((causes & NET_PAUSE_BACKGROUND) != 0)
    {
        why = "IN THE BACKGROUND";
    }
    float width = touch_ui_width(&app->ui);
    char text[128];
    if (why[0] != '\0')
    {
        std::snprintf(text, sizeof(text), "PAUSED ON %s: %s", selected_host(app), why);
    }
    else
    {
        std::snprintf(text, sizeof(text), "PAUSED ON %s", selected_host(app));
    }
    // A long host name: the cause matters more than the name.
    constexpr float MARGIN = 30.0f;   // clear of the quick menu's arrow at the top right
    if (touch_ui_text_width(&app->ui, text, TouchText::normal) > width - 2.0f * MARGIN)
    {
        if (why[0] != '\0')
        {
            std::snprintf(text, sizeof(text), "PAUSED: %s", why);
        }
        else
        {
            std::snprintf(text, sizeof(text), "PAUSED");
        }
    }
    float text_width = touch_ui_text_width(&app->ui, text, TouchText::normal);
    float text_height = touch_ui_text_height(&app->ui, TouchText::normal);
    float x = (width - text_width) / 2.0f;
    float y = 6.0f;
    touch_ui_rect(&app->ui, x - 4.0f, y - 2.0f, text_width + 8.0f, text_height + 4.0f, 0xB0000000u);
    touch_ui_text(&app->ui, x, y, text, app->ui.palette.text, TouchText::normal);
}

static void close_menu(App* app)
{
    if (app->menu_open && app->quick_menu.settings_changed)
    {
        save_settings(app);
        app->quick_menu.settings_changed = false;
    }
    app->menu_open = false;
}

static void open_menu(App* app)
{
    app->menu_open = true;
    quick_menu_open(&app->quick_menu);
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
    bool mode = screen == Screen::gamepad || screen == Screen::remote_play;
    if (app->screen == Screen::remote_play && screen != Screen::remote_play)
    {
        hosts_stream_stop(&app->hosts);   // the host stops sending; the next stream starts black
        stream_view_clear(&app->stream_view);
    }
    if (mode)
    {
        hosts_connect(&app->hosts, app->home.selected);
        hosts_request_devices(&app->hosts);   // the ports' tabs (sent once the link is up)
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
        app->pair_step = PairStep::pick;
        app->pair_internet = false;
        app->pair_host = -1;
        app->hosts.pair = PairStatus::idle;   // an old failure isn't this visit's
    }
    if (!mode)
    {
        app->devices_open = false;   // (between GAMEPAD and REMOTE PLAY it stays open)
    }
    app->screen = screen;
    close_menu(app);
}

// What a tap on a screen (or the quick menu) asked for.
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
                if (app->menu_open)
                {
                    close_menu(app);
                }
                else
                {
                    open_menu(app);
                }
            }
            else if (app->screen == Screen::home)
            {
                go_to(app, Screen::settings);
            }
            break;
        case NavAction::rotate:
            close_menu(app);
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
            // From the sheet: open it (FULL access only); from DEVICES: close it.
            close_menu(app);
            app->devices_open = !app->devices_open && selected_full_access(app);
            app->ports.view.has_pick = false;   // DEVICES opens with nothing picked
            ports_view_open_board(&app->ports.view);   // (slides up from the bottom)
            if (app->devices_open)
            {
                hosts_request_devices(&app->hosts);
            }
            break;
        case NavAction::edit_layout:
            close_menu(app);
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
        case NavAction::go_headless:
            close_menu(app);
            hosts_go_headless(&app->hosts, true);
            break;
        case NavAction::play_on_host:
            close_menu(app);
            hosts_go_headless(&app->hosts, false);
            break;
        case NavAction::touch_controls:
            app->touch_with_gamepad = !app->touch_with_gamepad;
            close_menu(app);
            break;
        case NavAction::pause:
            close_menu(app);
            hosts_toggle_pause(&app->hosts);
            break;
    }
}

// The back button (Android's, Escape on the desktop): closes the sheet or
// EDIT LAYOUT or HOME's sidebar, else goes back to HOME, else leaves the app.
static void go_back(App* app)
{
    if (app->menu_open)
    {
        close_menu(app);
    }
    else if (app->devices_open)
    {
        app->devices_open = false;
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

// A link naming a piccolo (net_pair_link.h: the Android intent, a dropped
// text, the browser's '#'). Not used for pairing since 2026-11-22 (the phone
// shows its code); kept for the links to come (Andrés: a game to join). For
// now it opens PAIR, and its beacon is the one INTERNET goes through.
static void open_link(App* app, const char* link)
{
    std::string beacon;
    if (!hosts_link_beacon(link, &beacon))
    {
        return;
    }
    if (app->screen != Screen::pair)
    {
        go_to(app, Screen::pair);
    }
    app->pair_beacon = beacon;
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
        // A link handed to the app while it runs (Android: piccoletto://pair,
        // PiccolettoActivity; the desktop: text dropped on the window).
        if ((event.type == SDL_EVENT_DROP_TEXT || event.type == SDL_EVENT_DROP_FILE) && event.drop.data != nullptr)
        {
            open_link(app, event.drop.data);
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
constexpr uint64_t BACK_HOLD_MS = 1000;
constexpr uint64_t START_HOLD_MS = 1000;   // opens the quick menu

static void update_gamepad_ui(App* app, uint64_t now_ms)
{
    RealGamepad* pads = &app->gamepad;
    if (pads->changed)
    {
        // Only ours (devices that aren't gamepads left out) drive ImGui.
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, pads->open, pads->count);
        pads->changed = false;
    }
    bool on_pad = (app->screen == Screen::gamepad || app->screen == Screen::remote_play) && !app->menu_open &&
                  !app->devices_open && !app->editor.active;
    ImGuiIO& io = ImGui::GetIO();
    if (on_pad)
    {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        if (input_hold_update(&app->back_hold, real_gamepad_held(pads, SDL_GAMEPAD_BUTTON_BACK), now_ms, BACK_HOLD_MS))
        {
            SDL_Log("BACK held: HOME");
            go_to(app, Screen::home);
        }
        // Holding START opens the quick menu (START reaches the host
        // meanwhile: the game may pause, which fits a menu opening).
        if (input_hold_update(&app->start_hold, real_gamepad_held(pads, SDL_GAMEPAD_BUTTON_START), now_ms, START_HOLD_MS))
        {
            SDL_Log("START held: quick menu");
            open_menu(app);
        }
        return;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    if (input_hold_fired(&app->back_hold) && real_gamepad_held(pads, SDL_GAMEPAD_BUTTON_BACK))
    {
        return;   // the BACK that just left a pad: not a press here
    }
    if (input_hold_fired(&app->start_hold))
    {
        if (real_gamepad_held(pads, SDL_GAMEPAD_BUTTON_START))
        {
            return;   // the START that opened the quick menu: not a press here
        }
        input_hold_reset(&app->start_hold);
    }
    input_hold_reset(&app->back_hold);
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
    bool playing = (app->screen == Screen::gamepad || app->screen == Screen::remote_play) && !app->menu_open &&
                   !app->devices_open && !app->editor.active;
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

// SEND CODE: the code to whoever sits at the piccolo. Android: its share
// sheet (PiccolettoActivity.shareText); elsewhere the clipboard.
static void send_code(const char* code)
{
    char text[160];
    std::snprintf(text, sizeof(text), "PICCOLETTO CODE: %.3s %.3s %.3s. TYPE IT ON THE PICCOLO IN TOOLS > INVITE.", code,
                  code + 3, code + 6);
#if defined(SDL_PLATFORM_ANDROID)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env != nullptr && activity != nullptr)
    {
        jclass type = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(type, "shareText", "(Ljava/lang/String;)V");
        if (method != nullptr)
        {
            jstring value = env->NewStringUTF(text);
            env->CallVoidMethod(activity, method, value);
            env->DeleteLocalRef(value);
        }
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            SDL_Log("SEND CODE: the share sheet failed");
        }
        env->DeleteLocalRef(type);
        env->DeleteLocalRef(activity);
    }
#else
    SDL_SetClipboardText(text);
#endif
}

// PAIR (Andrés, 2026-11-22): the phone shows its code, the piccolo's user
// types it. Waiting follows hosts.pair; PAIRED waits for CONTINUE, which goes
// to HOME with the new host on the card.
static NavAction draw_pair(App* app)
{
    Hosts* hosts = &app->hosts;
    const char* found[HOSTS_MAX_FOUND];
    int32_t found_count = hosts_unpaired_count(hosts);
    for (int32_t i = 0; i < found_count; i++)
    {
        found[i] = hosts_unpaired(hosts, i)->name;
    }
    if (hosts->pair == PairStatus::paired)
    {
        app->pair_step = PairStep::paired;
        app->pair_host = hosts->paired_index;
        hosts->pair = PairStatus::idle;
    }
    else if (app->pair_step != PairStep::paired)
    {
        app->pair_step = hosts->pair == PairStatus::busy ? PairStep::waiting : PairStep::pick;
    }
    // A failure: a new code for the next try (the old one is spent).
    if (hosts->pair == PairStatus::failed && app->last_pair != PairStatus::failed)
    {
        pair_screen_reset(&app->pair);
    }
    app->last_pair = hosts->pair;

    PairView view;
    view.step = app->pair_step;
    view.found = found;
    view.found_count = found_count;
    view.internet = app->pair_internet;
    if (!hosts->ok)
    {
        view.error = "NO NETWORK";
    }
    else if (hosts->pair == PairStatus::failed)
    {
        view.error = hosts_pair_failure_text(hosts);
    }
    if (view.step == PairStep::waiting)
    {
        char code[NET_PAIR_CODE_DIGITS + 1];
        if (hosts_pair_code(hosts, code))
        {
            std::memcpy(app->pair.code, code, sizeof(code));   // a new one after a wrong try there
        }
        view.host_asked = hosts_pair_waiting(hosts);
    }
    if (view.step == PairStep::paired && app->pair_host >= 0 &&
        app->pair_host < static_cast<int32_t>(hosts->known.size()))
    {
        const KnownHost* host = &hosts->known[static_cast<size_t>(app->pair_host)];
        view.host_name = host->name.c_str();
        view.remote_play = net_access_allows(host->access, NetAccessLevel::stream);
        view.devices = net_access_allows(host->access, NetAccessLevel::full);
    }

    PairTap tap = PairTap::none;
    NavAction action = pair_screen_draw(&app->pair, &app->ui, &view, &tap);
    switch (tap)
    {
    case PairTap::none:
        break;
    case PairTap::pair:
        app->pair_internet = false;
        if (app->pair.selected >= found_count ||
            !hosts_pair(hosts, hosts_unpaired(hosts, app->pair.selected)->address, app->pair.code))
        {
            hosts->pair = PairStatus::failed;
            hosts->refusal = NetClientRefusal::connection;
        }
        break;
    case PairTap::internet:
        app->pair_internet = true;
        if (!hosts_pair_internet(hosts, app->pair.code, app->pair_beacon.c_str()))
        {
            hosts->pair = PairStatus::failed;
            hosts->refusal = NetClientRefusal::beacon_unreachable;
        }
        break;
    case PairTap::cancel:
        hosts_cancel(hosts);
        pair_screen_reset(&app->pair);
        break;
    case PairTap::send_code:
        send_code(app->pair.code);
        break;
    case PairTap::proceed:
        app->home.selected = app->pair_host;
        action = NavAction::home;
        break;
    }
    return action;
}

// Who plays on the host's ports: its list (`known` once it came), or for
// testing without a host (--screen devices) an example list.
static std::vector<NetDevice>* ports_list(App* app, bool* known)
{
    std::vector<NetDevice>* example = &app->example_devices;
    if (!(app->testing && app->hosts.link != LinkStatus::ready))
    {
        *known = app->hosts.devices_known;
        return &app->hosts.devices;
    }
    if (example->empty())
    {
        static const char* const NAMES[] = {"8BITDO SN30 1", "MY PHONE 1", "ANA'S PHONE 1", "KEYBOARD 1"};
        static const uint8_t PORTS[] = {1, 2, 4, 1};
        for (uint32_t i = 0; i < 4; i++)
        {
            NetDevice device;
            device.controller = i;
            device.kind = i == 3 ? NetDeviceKind::keyboard : (i == 0 ? NetDeviceKind::gamepad : NetDeviceKind::network);
            device.ports = PORTS[i];
            device.owner = i == 1 ? NetDeviceOwner::yours : (i == 2 ? NetDeviceOwner::other : NetDeviceOwner::host);
            device.name = NAMES[i];
            example->push_back(device);
        }
    }
    *known = true;
    return example;
}

// The display's refresh: what a frame drawn with a preset must keep up with
// (60 when SDL doesn't know).
static double display_hz(App* app)
{
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app->shell.window));
    return mode != nullptr && mode->refresh_rate > 1.0f ? static_cast<double>(mode->refresh_rate) : 60.0;
}

// Every frame in REMOTE PLAY: one loop for the slow shader check, a sample
// only while the stream's picture is drawn (not while connecting or waiting).
static void measure_shader_check(App* app, bool drawing)
{
    double hz = display_hz(app);
    if (shader_check_loop(&app->shader_check, drawing, SDL_GetTicksNS(), 1e9 / hz))
    {
        app->shader_check_asked_ms = SDL_GetTicks();
        SDL_Log("Shader preset too slow: %.1f fps, target %.1f", 1e9 / app->shader_check.measured_ns, hz);
    }
}

// KEEP: the preset stays (and becomes the one to go back to). REVERT: the
// last preset that was fine, saved.
static void shader_check_answer(App* app, ShaderCheckChoice choice)
{
    shader_check_stop(&app->shader_check);
    if (choice == ShaderCheckChoice::revert)
    {
        app->settings.shader = app->shader_kept;
        app->shader_reverted = true;
        save_settings(app);
        SDL_Log("Shader preset too slow: reverted");
    }
    else
    {
        app->shader_kept = app->settings.shader;
    }
}

static void draw_screen(App* app)
{
    NavAction action = NavAction::none;
    int32_t forget = -1;
    // The pads take fingers only while nothing covers them.
    bool covered = app->menu_open || app->devices_open || (app->screen == Screen::home && app->home.sidebar_open);
    app->ui.fingers = covered ? nullptr : &app->fingers;
    // A real gamepad lights the pads' parts too (retroui's touch pad takes it as a RetroPad).
    real_gamepad_retropad(&app->gamepad, &app->gamepad_pad);
    app->ui.lit = &app->gamepad_pad;
    app->ui.action_lit = real_gamepad_held(&app->gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
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
        link.headless = app->hosts.headless_on && app->hosts.headless_yours;
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
            action = gamepad_screen_draw(&app->ui, hosts_pad_port(&app->hosts), &link,
                                         selected_allows(app, NetAccessLevel::stream), &app->layouts, &app->editor);
            break;
        case Screen::remote_play:
            action = remote_play_screen_draw(&app->ui, selected_host(app), hosts_pad_port(&app->hosts), &link,
                                             selected_allows(app, NetAccessLevel::stream), show_touch, &app->stream, &app->layouts,
                                             &app->editor);
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
    if ((app->screen == Screen::gamepad || app->screen == Screen::remote_play) && !app->editor.active)
    {
        draw_pause_banner(app);
    }
    touch_ui_end_screen();
    // The ports' tabs over the pads (any access level: read only), down
    // while the sheet or DEVICES covers them.
    if (app->screen == Screen::gamepad || app->screen == Screen::remote_play)
    {
        bool known = false;
        std::vector<NetDevice>* list = ports_list(app, &known);
        uint64_t now_ms = SDL_GetTicks();
        ports_overlay_update(&app->ports, list, known, &link, now_ms);
        ports_overlay_draw_tabs(&app->ports, &app->ui, app->menu_open || app->devices_open, now_ms);
    }
    else if (app->ports.known)
    {
        ports_overlay_reset(&app->ports);
    }
    // HOME's sidebar (landscape) the same way, above HOME.
    if (app->screen == Screen::home && app->home.sidebar_open)
    {
        touch_ui_begin_overlay(&app->ui, "##sidebar_window");
        home_sidebar_draw(&app->home, &app->ui, shown, shown_count);
        touch_ui_end_screen();
    }
    // DEVICES over the pads (the game shows through, dimmed). A window under
    // it takes the taps away from the screen; the board reads them itself.
    if (app->devices_open && !app->menu_open)
    {
        touch_ui_begin_overlay(&app->ui, "##devices_window");
        // The pads dimmed behind it (the board doesn't dim on its own).
        touch_ui_rect(&app->ui, 0.0f, 0.0f, touch_ui_width(&app->ui), touch_ui_height(&app->ui), 0x50000000u);
        touch_ui_end_screen();
        bool known = false;
        std::vector<NetDevice>* list = ports_list(app, &known);
        PortsOverlayMove move;
        NavAction board = ports_overlay_draw_board(&app->ports, &app->ui, SDL_GetTicks(), list, known, &move);
        action = board != NavAction::none ? board : action;
        if (move.move && list == &app->example_devices)
        {
            // The example list: moved here (-1 = on no port: out of the list's ports).
            (*list)[static_cast<size_t>(move.index)].ports = static_cast<uint8_t>(move.port < 0 ? 0u : 1u << move.port);
        }
        else if (move.move && move.index >= 0 && move.index < static_cast<int32_t>(app->hosts.devices.size()))
        {
            hosts_move_device(&app->hosts, &app->hosts.devices[static_cast<size_t>(move.index)], move.port);
        }
    }
    // The quick menu in its own window above the screen: the screen behind takes no taps.
    if (app->menu_open)
    {
        touch_ui_begin_overlay(&app->ui, "##quick_menu_window");
        QuickMenuInput input;
        input.current = app->screen;
        input.host = selected_host(app);
        input.online = app->hosts.link == LinkStatus::ready;
        input.devices = selected_full_access(app);
        input.touch_toggle = app->screen == Screen::remote_play && gamepad_connected;
        input.touch_shown = show_touch;
        if (app->screen == Screen::remote_play && hosts_streaming(&app->hosts))
        {
            if (!app->hosts.headless_on)
            {
                input.headless = HeadlessItem::go;
            }
            else if (app->hosts.headless_yours)
            {
                input.headless = HeadlessItem::give_back;
            }
        }
        uint32_t pause_causes = 0;
        input.pause = hosts_pause(&app->hosts, &pause_causes);
        input.paused = (pause_causes & NET_PAUSE_USER) != 0;
        action = quick_menu_draw(&app->quick_menu, &app->ui, &input, &app->settings);
        touch_ui_end_screen();
    }
    // A preset too slow for this phone: KEEP / REVERT above everything (the
    // quick menu too: the preset may have been stepped to from it).
    if (app->screen == Screen::remote_play && app->shader_check.state == ShaderCheckState::asking)
    {
        ShaderCheckText words;   // (piccoletto's UI is English for now)
        const char* name = shader_preset(app->settings.shader_gl, app->settings.shader)->label;
        ShaderCheckChoice answer = shader_check_view_draw_sheet(&app->ui, &words, name, 1e9 / app->shader_check.measured_ns,
                                                                display_hz(app), app->shader_check_asked_ms, SDL_GetTicks());
        if (answer != ShaderCheckChoice::none)
        {
            shader_check_answer(app, answer);
        }
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
    static const char* const NAMES[] = {"home", "pair", "gamepad", "remote_play", "settings"};
    static const Screen SCREENS[] = {Screen::home, Screen::pair, Screen::gamepad, Screen::remote_play, Screen::settings};
    for (int32_t i = 0; i < 5; i++)
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
    if (app_shell_frames_in_flight(&app->shell) != in_flight || app_shell_wait_for_present(&app->shell) != start_late)
    {
        app_shell_set_frames_in_flight(&app->shell, in_flight);
        app_shell_set_wait_for_present(&app->shell, start_late);
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
        if (app->shader_check.state == ShaderCheckState::asking)
        {
            shader_check_answer(app, ShaderCheckChoice::revert);   // left without answering: the safe choice
        }
        app->shader_check.last_ns = 0;   // (the next loop in REMOTE PLAY isn't a sample)
        return;
    }
    StreamView* view = &app->stream_view;
    // The shader preset: loaded only when the choice changes (it waits for the GPU).
    if (app->settings.shader != app->shader_applied)
    {
        // A change (not the first load): measure the new preset; NONE needs no check.
        if (app->shader_applied >= 0 && app->settings.shader != 0 && !app->shader_reverted)
        {
            if (app->shader_check.state == ShaderCheckState::idle)
            {
                app->shader_kept = app->shader_applied;
            }
            shader_check_start(&app->shader_check);
        }
        else
        {
            shader_check_stop(&app->shader_check);
        }
        app->shader_reverted = false;
        app->shader_applied = app->settings.shader;
        const ShaderPreset* preset = shader_preset(app->settings.shader_gl, app->settings.shader);
        app->shader_path = preset->key[0] != '\0' ? app->presets_dir + "/" + preset->key : std::string();
        stream_view_set_shader(view, &app->shader_library, &app->shell, app->shader_path.c_str());
    }
    stream_view_update(view, &app->hosts, &app->shell);
    measure_shader_check(app, view->has_picture);
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

// One frame: called by app_loop_run until it returns false (a plain loop on
// the desktop and Android, the browser's animation frames in the web build).
static bool app_frame(void* data)
{
    App* app = static_cast<App*>(data);
    if (!app->running)
    {
        return false;
    }
    handle_events(app);
    if (!app_shell_begin_frame(&app->shell))
    {
        return app->running;
    }
    const ImGuiIO& io = ImGui::GetIO();
    app->ui.scale = touch_ui_scale_for(io.DisplaySize.x, io.DisplaySize.y);
    apply_settings(app);
    real_gamepad_update(&app->gamepad);
    double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
    update_gamepad_ui(app, SDL_GetTicks());
    hosts_update(&app->hosts, now);
    // The modes need the host (Andrés): when the connection drops, or
    // can't be made, back to HOME.
    bool in_mode = app->screen == Screen::gamepad || app->screen == Screen::remote_play;
    if (in_mode && app->hosts.link == LinkStatus::failed && !app->testing)
    {
        SDL_Log("Network: lost %s (%s): HOME", selected_host(app), hosts_refusal_text(app->hosts.refusal));
        go_to(app, Screen::home);
    }
    // The host changed this phone's level while it plays (the access
    // event; the host already stopped what the new level doesn't allow).
    if (app->screen == Screen::remote_play && !selected_allows(app, NetAccessLevel::stream) && !app->testing)
    {
        SDL_Log("Network: %s took REMOTE PLAY away: GAMEPAD", selected_host(app));
        go_to(app, Screen::gamepad);
    }
    if (app->devices_open && !selected_full_access(app) && !app->testing)
    {
        app->devices_open = false;
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
    ImVec4 background_color = retro_ui_color_vec4(background);
    ClearColor clear = {background_color.x, background_color.y, background_color.z, 1.0f};
    if (!app_shell_present(&app->shell, clear))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
        return false;
    }
    return app->running;
}

// The browser is leaving (app_loop_on_leave): what piccoletto writes when a
// screen closes, written now, since its exit never runs there (a layout being
// edited, SETTINGS). The paired hosts are written when they change.
static void app_save_on_leave(void* data, bool unloading)
{
    (void)unloading;
    App* app = static_cast<App*>(data);
    save_settings(app);
    save_layouts(app);
    app_storage_flush();
}

int main(int argc, char** argv)
{
    SDL_Log("piccoletto %s", PICCOLETTO_VERSION);
    app_storage_init("piccoletto");   // the browser: piccoletto's folder on OPFS

    App* app = new App{};
    pad_layouts_init(&app->layouts);

    Screen first = Screen::home;
    std::string link_beacon;   // opened with a link: its beacon (open_link)
    bool landscape = false;
    bool edit = false;
    bool open_devices = false;
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
        else if (SDL_strcmp(argv[i], "--screen") == 0 && i + 1 < argc && SDL_strcmp(argv[i + 1], "devices") == 0)
        {
            // DEVICES over GAMEPAD, with an example list (no host needed).
            first = Screen::gamepad;
            app->testing = true;
            open_devices = true;
            i++;
        }
        else if (SDL_strcmp(argv[i], "--pair-link") == 0 && i + 1 < argc && hosts_link_beacon(argv[i + 1], &link_beacon))
        {
            // Opened with a link (Android: piccoletto://pair): PAIR, its
            // beacon the one INTERNET goes through (open_link).
            first = Screen::pair;
            i++;
        }
        else if (SDL_strcmp(argv[i], "--aspect") == 0 && i + 1 < argc && parse_aspect(argv[i + 1], &app->stream))
        {
            i++;
        }
        else
        {
            SDL_Log("Usage: piccoletto [--screen home|pair|gamepad|remote_play|devices|settings] [--landscape] [--aspect W:H] [--edit] [--pair-link LINK]");
        }
    }

    // A phone-shaped window on the desktop (2 window pixels per dp).
    AppShellConfig config = {};
    config.name          = "piccoletto";
    config.version       = PICCOLETTO_VERSION;
    config.identifier    = "site.multitap.piccoletto";
    config.icons         = app_icon_images;   // the window's icon (the taskbar, Alt-Tab)
    config.icon_count    = app_icon_image_count;
    config.window_width  = landscape ? 900 : 420;
    config.window_height = landscape ? 420 : 900;

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
    // A build from a branch other than main has the branch in its version
    // ("0.1-piccoletto"): show it in the window title, so a test build says
    // which branch made it (as piccolo does).
    if (SDL_strchr(PICCOLETTO_VERSION, '-') != nullptr)
    {
        SDL_SetWindowTitle(app->shell.window, "piccoletto " PICCOLETTO_VERSION);
    }
    app->ui.font = pixel_font_imgui_add();
    if (app->ui.font == nullptr)
    {
        app_shell_destroy(&app->shell);
        delete app;
        return 1;
    }
    app_loop_prepare_window(app->shell.window);
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
    // The shader list follows the GPU path (OpenGL ES: .glpreset).
    app->settings.shader_gl = app->shell.api == GpuApi::gl;
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
            app->presets_dir = std::string(base) + "shaders/" + shader_presets_folder(app->settings.shader_gl);
        }
        shader_library_load(&app->shader_library, folders);
#if defined(SDL_PLATFORM_ANDROID)
        // The presets live in the APK; librashader reads plain files.
        if (!app->settings_dir.empty())
        {
            std::string root = app->settings_dir + "/../";
            const char* const asset_folders[1] = {"shaders"};
            app_assets_unpack(asset_folders, 1, root.c_str(), app_assets_apk_stamp());
            app->presets_dir = root + "shaders/" + shader_presets_folder(app->settings.shader_gl);
        }
#endif
    }

#if defined(SDL_PLATFORM_EMSCRIPTEN)
    // The browser: a page opened with a link after '#' (open_link) opens PAIR.
    {
        const char* hash = emscripten_run_script_string("location.hash");
        if (hash != nullptr && hosts_link_beacon(hash, &link_beacon))
        {
            first = Screen::pair;
        }
    }
#endif
    go_to(app, first);
    app->pair_beacon = link_beacon;
    app->editor.active = edit && (first == Screen::gamepad || first == Screen::remote_play);
    app->devices_open = open_devices;
    app->running = true;
    app_loop_on_leave(app_save_on_leave, app);
    app_loop_run(app_frame, app);   // the browser: never returns

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
