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

// `port`: 1-based, as shown (the host is named in the quick menu). While editor->active the
// current layout is edited instead of played (NavAction::edit_done at DONE).
// `hotkeys`: the host lets this phone use them (REMOTE PLAY granted: the HOTKEY button).
// `link`: the connection to the host (ping, loss), for the status line.
NavAction gamepad_screen_draw(const TouchUi* ui, int32_t port, const PadLink* link,
                              bool hotkeys, PadLayouts* layouts, PadEditor* editor);
