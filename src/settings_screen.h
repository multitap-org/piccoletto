#pragma once

// SETTINGS (HOME's ☰): this phone's name, theme, language, vibrate on
// press, the overlays' default opacity, remote play's default quality, the
// paired hosts (FORGET), about. One column under a fixed header; drag to
// scroll when it doesn't fit.

#include <cstdint>

#include "app_settings.h"
#include "screens.h"
#include "touch_ui.h"

struct SettingsScreen
{
    float scroll = 0.0f;              // retro pixels
    bool  theme_list_open = false;    // THEME's list, over the screen
};

// `paired`: the paired hosts' names. *forget is set to the index of the one
// whose FORGET was tapped (-1 if none). Changes go straight into *settings.
NavAction settings_screen_draw(SettingsScreen* screen, const TouchUi* ui, AppSettings* settings, const char* version,
                               const char* const* paired, int32_t paired_count, int32_t* forget);

// THEME's list over SETTINGS while screen->theme_list_open, in its own overlay
// (call after the screen's window): a tap on a theme picks it and closes the
// list, a tap outside closes it.
void settings_screen_draw_theme_list(SettingsScreen* screen, const TouchUi* ui, AppSettings* settings);
