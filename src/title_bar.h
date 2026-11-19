#pragma once

// The title bar's icons, top right, in the same place on HOME, GAMEPAD and
// REMOTE PLAY (Andrés, 2026-11-19): + (PAIR A DEVICE, with how many
// unpaired devices were found), rotate, ☰. On the pads + is shown but off.
// The left side is the screen's (the name now, a logo later).

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

constexpr float TITLE_BAR_MARGIN = 8.0f;    // retro pixels from the screen's edges
constexpr float TITLE_BAR_ICON   = 24.0f;   // each icon's square
// Its top: 6 retro pixels in landscape (phones have little height then), 8 in portrait.
float title_bar_y(const TouchUi* ui);
// Where the screen's content can start under the title bar.
float title_bar_bottom(const TouchUi* ui);
// The left edge of the icons (the screen's own title must end before it).
float title_bar_icons_x(const TouchUi* ui);

// Draws the three icons. `pair_enabled` false: + is drawn off and takes no
// taps. `found_count` > 0 puts that number by the +.
NavAction title_bar_icons(const TouchUi* ui, bool pair_enabled, int32_t found_count);
