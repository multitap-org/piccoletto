#pragma once

// GAMEPAD: the phone as a controller for the host. piccolo's pad picture at
// phone size: portrait = a header line and the pad below; landscape = the
// pad picture's own shape (shoulders on top, the body below).
// Not sending anything yet (no networking).

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

// `host`: its name; `port`: 1-based, as shown.
NavAction gamepad_screen_draw(const TouchUi* ui, const char* host, int32_t port);
