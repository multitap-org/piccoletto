#pragma once

// GAMEPAD: the phone as a controller for the host. piccolo's pad picture at
// phone size: portrait = a header line and the pad below; landscape = the
// pad picture's own shape (shoulders on top, the body below). The pad's
// groups can be moved and resized (EDIT LAYOUT).
// Not sending anything yet (no networking).

#include <cstdint>

#include "pad_editor.h"
#include "pad_layouts.h"
#include "screens.h"
#include "touch_ui.h"

// `host`: its name; `port`: 1-based, as shown. While editor->active the
// current layout is edited instead of played (NavAction::edit_done at DONE).
// `full_access`: the host gave this phone every mode (the HOTKEY button).
// `link`, `ping_ms`: the connection to the host, for the status line.
NavAction gamepad_screen_draw(const TouchUi* ui, const char* host, int32_t port, LinkShown link, uint32_t ping_ms,
                              bool full_access, PadLayouts* layouts, PadEditor* editor);
