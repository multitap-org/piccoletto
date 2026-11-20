#pragma once

// The ☰ sheet over GAMEPAD and REMOTE PLAY: switch between the two, EDIT
// LAYOUT, DEVICES (FULL access), the touch controls with a real gamepad,
// rotate, leave to HOME. The screen behind is dimmed and doesn't take taps.

#include "screens.h"
#include "touch_ui.h"

// `current`: the screen it's open over (its own switch isn't offered).
// `touch_toggle`: offer SHOW / HIDE TOUCH CONTROLS (REMOTE PLAY with a real
// gamepad), `touch_shown` says which. `devices`: offer DEVICES. Returns what
// was picked; NavAction::menu = close the sheet.
NavAction menu_sheet_draw(const TouchUi* ui, Screen current, bool touch_toggle, bool touch_shown, bool devices);
