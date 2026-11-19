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
#include "home_screen.h"
#include "host_fs.h"
#include "menu_sheet.h"
#include "pad_editor.h"
#include "pad_layouts.h"
#include "pair_screen.h"
#include "piccoletto_version.h"
#include "pixel_font.h"
#include "pixel_font_imgui.h"
#include "remote_play_screen.h"
#include "retro_ui.h"
#include "screens.h"
#include "settings_file.h"
#include "settings_screen.h"
#include "touch_ui.h"

// Placeholder hosts until pairing exists: what HOME looks like with a few
// (the mockups' examples). Access is what each host would have granted.
static const HomeHost PLACEHOLDER_HOSTS[] = {
    {"LIVING ROOM PC", true, "SUPER METROID", {true, true, true}},
    {"ARM BOARD", true, "", {true, false, false}},
    {"LAPTOP", false, "", {true, true, false}},
};
constexpr int32_t PLACEHOLDER_HOST_COUNT = static_cast<int32_t>(sizeof(PLACEHOLDER_HOSTS) / sizeof(PLACEHOLDER_HOSTS[0]));
static const char* const PLACEHOLDER_HOST_NAMES[] = {"LIVING ROOM PC", "ARM BOARD", "LAPTOP"};
// Unpaired hosts "found on the network".
static const char* const PLACEHOLDER_FOUND[] = {"BEDROOM PC", "HANDHELD PC"};
constexpr int32_t PLACEHOLDER_FOUND_COUNT = static_cast<int32_t>(sizeof(PLACEHOLDER_FOUND) / sizeof(PLACEHOLDER_FOUND[0]));
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
    std::string    settings_path;     // settings/piccoletto.cfg ("" = nothing is saved)
    std::string    layouts_path;      // settings/layouts.cfg
    SettingsFile   settings_file;
    SettingsFile   layouts_file;
    bool           running    = false;
};

static const char* selected_host(const App* app)
{
    return PLACEHOLDER_HOSTS[app->home.selected].name;
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

static void draw_screen(App* app)
{
    NavAction action = NavAction::none;
    bool done = false;
    int32_t forget = -1;
    touch_ui_begin_screen(&app->ui, "##screen");
    switch (app->screen)
    {
        case Screen::home:
            action = home_screen_draw(&app->home, &app->ui, PLACEHOLDER_HOSTS, PLACEHOLDER_HOST_COUNT, PLACEHOLDER_FOUND_COUNT);
            break;
        case Screen::pair:
            action = pair_screen_draw(&app->pair, &app->ui, PLACEHOLDER_FOUND, PLACEHOLDER_FOUND_COUNT, &done);
            if (done)
            {
                // The request goes to the host with networking.
                SDL_Log("PAIR: %s with code %s", PLACEHOLDER_FOUND[app->pair.selected], app->pair.code);
                action = NavAction::home;
            }
            break;
        case Screen::gamepad:
            action = gamepad_screen_draw(&app->ui, selected_host(app), PLACEHOLDER_PORT, &app->layouts, &app->editor);
            break;
        case Screen::remote_play:
            action = remote_play_screen_draw(&app->ui, selected_host(app), PLACEHOLDER_PORT, &app->stream, &app->layouts, &app->editor);
            break;
        case Screen::devices:
            action = devices_screen_draw(&app->devices, &app->ui, selected_host(app));
            break;
        case Screen::settings:
            action = settings_screen_draw(&app->settings_screen, &app->ui, &app->settings, PICCOLETTO_VERSION,
                                          PLACEHOLDER_HOST_NAMES, PLACEHOLDER_HOST_COUNT, &forget);
            if (forget >= 0)
            {
                // Forgetting tells the host too, with networking.
                SDL_Log("FORGET: %s", PLACEHOLDER_HOST_NAMES[forget]);
            }
            break;
    }
    touch_ui_end_screen();
    // HOME's sidebar (landscape) the same way, above HOME.
    if (app->screen == Screen::home && app->home.sidebar_open)
    {
        touch_ui_begin_overlay(&app->ui, "##sidebar_window");
        home_sidebar_draw(&app->home, &app->ui, PLACEHOLDER_HOSTS, PLACEHOLDER_HOST_COUNT);
        touch_ui_end_screen();
    }
    // The sheet in its own window above the screen: the screen behind takes no taps.
    if (app->sheet_open)
    {
        touch_ui_begin_overlay(&app->ui, "##sheet_window");
        action = menu_sheet_draw(&app->ui, app->screen);
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
        draw_screen(app);

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
    app_shell_destroy(&app->shell);
    delete app;
    return 0;
}
