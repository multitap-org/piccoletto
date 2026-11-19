// piccoletto: piccolo's companion app (see CLAUDE.md). For now: the window,
// the theme and every screen with placeholder data; no networking yet.

#include <cstdlib>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

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
#include "remote_play_screen.h"
#include "retro_ui.h"
#include "screens.h"
#include "settings_file.h"
#include "settings_screen.h"
#include "touch_fingers.h"
#include "touch_ui.h"

// The port this phone's pad plays on: from the host's controllers message, later.
constexpr int32_t PLACEHOLDER_PORT = 1;

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
    RemoteStream   stream     = {};   // placeholder until there's a stream
    AppSettings    settings   = {};
    TouchFingers   fingers    = {};   // every finger, for the pads
    Haptics        haptics    = {};   // VIBRATE ON PRESS
    RealGamepad    gamepad    = {};   // gamepads connected to the phone
    bool           touch_with_gamepad = false;   // REMOTE PLAY: touch controls shown although a gamepad is connected
    Hosts          hosts;             // the paired hosts, discovery, pairing, the connection
    PairStatus     last_pair  = PairStatus::idle;   // hosts.pair last frame (PAIR clears the code when it fails)
    std::string    settings_dir;      // settings/ ("" = nothing is saved)
    std::string    settings_path;     // settings/piccoletto.cfg ("" = nothing is saved)
    std::string    layouts_path;      // settings/layouts.cfg
    SettingsFile   settings_file;
    SettingsFile   layouts_file;
    bool           running    = false;
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
    if (mode)
    {
        hosts_connect(&app->hosts, app->home.selected);
    }
    else if (screen != app->screen)
    {
        hosts_disconnect(&app->hosts);
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
        NetClientState state = hosts->client.state;
        progress.text = state == NetClientState::connecting || state == NetClientState::waiting ? "CONNECTING…" : "CHECKING THE CODE…";
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
    // REMOTE PLAY: the touch controls hide while a real gamepad is connected (unless shown from the sheet).
    bool gamepad_connected = real_gamepad_connected(&app->gamepad);
    bool show_touch = !gamepad_connected || app->touch_with_gamepad || app->editor.active;
    HomeHost shown[MAX_SHOWN_HOSTS];
    int32_t shown_count = home_hosts(app, shown);
    uint32_t ping_ms = 0;
    LinkShown link = hosts_link_shown(&app->hosts, &ping_ms);
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
            action = gamepad_screen_draw(&app->ui, selected_host(app), PLACEHOLDER_PORT, link, ping_ms,
                                         selected_full_access(app), &app->layouts, &app->editor);
            break;
        case Screen::remote_play:
            action = remote_play_screen_draw(&app->ui, selected_host(app), PLACEHOLDER_PORT, link, ping_ms,
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
    config.identifier    = "org.multitap.piccoletto";
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
    app->ui.font = pixel_font_imgui_add();
    if (app->ui.font == nullptr)
    {
        app_shell_destroy(&app->shell);
        delete app;
        return 1;
    }
    ImGui::GetIO().IniFilename = nullptr;   // no window layout to remember: every screen is full screen
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
        hosts_update(&app->hosts, static_cast<double>(SDL_GetTicks()) / 1000.0);
        draw_screen(app);
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
    real_gamepad_close(&app->gamepad);
    app_shell_destroy(&app->shell);
    delete app;
    return 0;
}
