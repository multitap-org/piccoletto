#pragma once

// The ☰ sheet over GAMEPAD and REMOTE PLAY: switch between the two, rotate,
// leave to HOME. The screen behind is dimmed and doesn't take taps.

#include "screens.h"
#include "touch_ui.h"

// `current`: the screen it's open over (its own switch isn't offered).
// Returns what was picked; NavAction::menu = close the sheet.
NavAction menu_sheet_draw(const TouchUi* ui, Screen current);
