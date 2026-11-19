#pragma once

// REMOTE PLAY: the host's game on the phone, inside a minimal bezel flush
// with the phone's edges (portrait: the screen on top, the controls below;
// landscape: the screen in the middle, d-pad left, buttons right).
// No video yet: the screen shows where the game goes and its size.

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

NavAction remote_play_screen_draw(const TouchUi* ui, const char* host);
