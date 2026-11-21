#pragma once

// The quick menu over GAMEPAD and REMOTE PLAY (Andrés, 2026-11-20; the
// mockup: https://claude.ai/artifact/53qEApFSH3rxkxRHUyHjJo): a sidebar that
// slides in from the right. It replaces the pads' title bar and the old ☰
// sheet: the host, switch to the other mode, REMOTE PLAY's SHADER and PACING,
// EDIT LAYOUT, DEVICES (FULL access), SHOW / HIDE TOUCH CONTROLS (a real
// gamepad in REMOTE PLAY), ROTATE, LEAVE. The screen behind is dimmed a
// little and takes no taps; a tap outside, the › or B closes it.
//
// Opened by the see-through arrow at the top right (quick_menu_arrow, drawn
// by the pads instead of the title bar) or by holding START on a gamepad
// (main.cpp; START reaches the host meanwhile, so the game may pause, which
// fits a menu opening).

#include <cstdint>

#include "app_settings.h"
#include "screens.h"
#include "touch_ui.h"

struct QuickMenu
{
    uint64_t opened_ms = 0;          // when it was opened: it slides in from then (retro_ui_ease_out)
    bool  settings_changed = false;  // SHADER or PACING changed: save them when it closes
};

// GO HEADLESS's item (REMOTE PLAY while streaming).
enum class HeadlessItem
{
    none,
    go,          // GO HEADLESS
    give_back,   // PLAY ON <host>
};

struct QuickMenuInput
{
    Screen      current = Screen::gamepad;   // the screen it's over (it offers the other one)
    const char* host = "";
    bool        online = false;
    bool        devices = false;             // FULL access: DEVICES
    bool        touch_toggle = false;        // REMOTE PLAY with a real gamepad: SHOW / HIDE TOUCH CONTROLS
    bool        touch_shown = false;
    HeadlessItem headless = HeadlessItem::none;   // `host` names the host for giving it back
};

// Starts it sliding in from the right.
void quick_menu_open(QuickMenu* menu);

// Draws it (in its own overlay window). SHADER and PACING change `settings`
// directly. Returns what was picked; NavAction::menu = close it.
NavAction quick_menu_draw(QuickMenu* menu, const TouchUi* ui, const QuickMenuInput* input, AppSettings* settings);

// The pads' see-through arrow at the top right: true when tapped.
bool quick_menu_arrow(const TouchUi* ui);
