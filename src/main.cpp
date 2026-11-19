// piccoletto: piccolo's companion app (see CLAUDE.md). For now: the window,
// the theme and every screen with placeholder data; no networking yet.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app_shell.h"
#include "devices_screen.h"
#include "gamepad_screen.h"
#include "home_screen.h"
#include "menu_sheet.h"
#include "pair_screen.h"
#include "piccoletto_version.h"
#include "pixel_font.h"
#include "pixel_font_imgui.h"
#include "remote_play_screen.h"
#include "retro_ui.h"
#include "screens.h"
#include "touch_ui.h"

// Placeholder hosts until pairing exists: what HOME looks like with a few
// (the mockups' examples). Access is what each host would have granted.
static const HomeHost PLACEHOLDER_HOSTS[] = {
    {"LIVING ROOM PC", true, "SUPER METROID", {true, true, true}},
    {"ARM BOARD", true, "", {true, false, false}},
    {"LAPTOP", false, "", {true, true, false}},
};
constexpr int32_t PLACEHOLDER_HOST_COUNT = static_cast<int32_t>(sizeof(PLACEHOLDER_HOSTS) / sizeof(PLACEHOLDER_HOSTS[0]));
// Unpaired hosts "found on the network".
static const char* const PLACEHOLDER_FOUND[] = {"BEDROOM PC", "HANDHELD PC"};
constexpr int32_t PLACEHOLDER_FOUND_COUNT = static_cast<int32_t>(sizeof(PLACEHOLDER_FOUND) / sizeof(PLACEHOLDER_FOUND[0]));
constexpr int32_t PLACEHOLDER_PORT = 1;

struct App
{
    AppShell      shell      = {};
    TouchUi       ui         = {};
    Screen        screen     = Screen::home;
    bool          sheet_open = false;   // the ☰ sheet over GAMEPAD / REMOTE PLAY
    HomeScreen    home       = {};
    PairScreen    pair       = {};
    DevicesScreen devices    = {};
    bool          running    = false;
};

static const char* selected_host(const App* app)
{
    return PLACEHOLDER_HOSTS[app->home.selected].name;
}

// Turns the screen: on the desktop, swaps the window's width and height.
// (On a phone this will lock the orientation instead.)
static void rotate(App* app)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(app->shell.window, &width, &height);
    SDL_SetWindowSize(app->shell.window, height, width);
}

static void go_to(App* app, Screen screen)
{
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
            else
            {
                SDL_Log("HOME: the menu (settings) isn't there yet");
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
    }
}

// The back button (Android's, Escape on the desktop): closes the sheet, else
// goes back to HOME, else leaves the app.
static void go_back(App* app)
{
    if (app->sheet_open)
    {
        app->sheet_open = false;
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
        app_shell_process_event(&app->shell, &event);
        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.key == SDLK_AC_BACK || event.key.key == SDLK_ESCAPE))
        {
            go_back(app);
        }
    }
}

static void draw_screen(App* app)
{
    NavAction action = NavAction::none;
    bool done = false;
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
            action = gamepad_screen_draw(&app->ui, selected_host(app), PLACEHOLDER_PORT);
            break;
        case Screen::remote_play:
            action = remote_play_screen_draw(&app->ui, selected_host(app));
            break;
        case Screen::devices:
            action = devices_screen_draw(&app->devices, &app->ui, selected_host(app));
            break;
    }
    touch_ui_end_screen();
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

// Desktop testing: --screen home|pair|gamepad|remote_play|devices opens on
// that screen, --landscape starts with the window turned.
static bool parse_screen(const char* name, Screen* screen)
{
    static const char* const NAMES[] = {"home", "pair", "gamepad", "remote_play", "devices"};
    static const Screen SCREENS[] = {Screen::home, Screen::pair, Screen::gamepad, Screen::remote_play, Screen::devices};
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

int main(int argc, char** argv)
{
    SDL_Log("piccoletto %s", PICCOLETTO_VERSION);

    App* app = new App{};
    devices_screen_init(&app->devices);

    Screen first = Screen::home;
    bool landscape = false;
    for (int i = 1; i < argc; i++)
    {
        if (SDL_strcmp(argv[i], "--landscape") == 0)
        {
            landscape = true;
        }
        else if (SDL_strcmp(argv[i], "--screen") == 0 && i + 1 < argc && parse_screen(argv[i + 1], &first))
        {
            i++;
        }
        else
        {
            SDL_Log("Usage: piccoletto [--screen home|pair|gamepad|remote_play|devices] [--landscape]");
        }
    }

    // A phone-shaped window on the desktop (portrait, 2 window pixels per dp).
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
    app->ui.palette = *retro_ui_theme_palette(RetroUiTheme::blue);

    go_to(app, first);
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

    app_shell_destroy(&app->shell);
    delete app;
    return 0;
}
