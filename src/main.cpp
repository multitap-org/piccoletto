// piccoletto: piccolo's companion app (see CLAUDE.md). For now: the window,
// the theme and the HOME screen with placeholder hosts; no networking yet.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app_shell.h"
#include "home_screen.h"
#include "piccoletto_version.h"
#include "pixel_font.h"
#include "pixel_font_imgui.h"
#include "retro_ui.h"
#include "touch_ui.h"

// Placeholder hosts until pairing exists: what HOME looks like with a few
// (the mockups' examples). Access is what each host would have granted.
static const HomeHost PLACEHOLDER_HOSTS[] = {
    {"LIVING ROOM PC", true, "SUPER METROID", {true, true, true}},
    {"ARM BOARD", true, "", {true, false, false}},
    {"LAPTOP", false, "", {true, true, false}},
};
constexpr int32_t PLACEHOLDER_HOST_COUNT = static_cast<int32_t>(sizeof(PLACEHOLDER_HOSTS) / sizeof(PLACEHOLDER_HOSTS[0]));
constexpr int32_t PLACEHOLDER_FOUND = 1;

struct App
{
    AppShell   shell   = {};
    TouchUi    ui      = {};
    HomeScreen home    = {};
    bool       running = false;
};

static const char* action_name(HomeAction action)
{
    switch (action)
    {
        case HomeAction::none:        return "NONE";
        case HomeAction::gamepad:     return "GAMEPAD";
        case HomeAction::remote_play: return "REMOTE PLAY";
        case HomeAction::devices:     return "DEVICES";
        case HomeAction::pair:        return "PAIR A DEVICE";
        case HomeAction::menu:        return "MENU";
        default:                      return "?";
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
        // Android's back button arrives as this key: on HOME it leaves the app.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_AC_BACK)
        {
            app->running = false;
        }
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    SDL_Log("piccoletto %s", PICCOLETTO_VERSION);

    App* app = new App{};

    // A phone-shaped window on the desktop (portrait, 2 window pixels per dp).
    AppShellConfig config = {};
    config.name          = "piccoletto";
    config.version       = PICCOLETTO_VERSION;
    config.identifier    = "org.multitap.piccoletto";
    config.window_width  = 420;
    config.window_height = 900;

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

        touch_ui_begin_screen(&app->ui, "##home");
        HomeAction action = home_screen_draw(&app->home, &app->ui, PLACEHOLDER_HOSTS, PLACEHOLDER_HOST_COUNT,
                                             PLACEHOLDER_FOUND);
        touch_ui_end_screen();
        if (action != HomeAction::none)
        {
            // The screens behind these don't exist yet.
            SDL_Log("HOME: %s on %s", action_name(action), PLACEHOLDER_HOSTS[app->home.selected].name);
        }

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
