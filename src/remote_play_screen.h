#pragma once

// REMOTE PLAY: the host's game on the phone, always as big as the screen
// allows for the stream's aspect (never assumed: 4:3, 8:7, 5:4, the DS's two
// screens...). Portrait: the picture across the top, the pad shrunk into the
// room left under it. When there isn't enough room (landscape, or a picture
// taller than wide in portrait) the pad becomes an overlay over the picture,
// see-through, movable and resizable (EDIT LAYOUT).
// No video yet: the picture's place shows the stream's size and aspect.

#include <cstdint>

#include "pad_editor.h"
#include "pad_layouts.h"
#include "screens.h"
#include "touch_ui.h"

// What the host streams (placeholder values until there's a stream).
struct RemoteStream
{
    int32_t     width  = 256;
    int32_t     height = 224;
    float       aspect = 4.0f / 3.0f;   // the picture's shape on screen (width / height)
    const char* aspect_label = "4:3";
};

// `full_access`: the host gave this phone every mode (the HOTKEY button).
// `show_touch` false: the touch controls aren't drawn (a real gamepad is
// connected); EDIT LAYOUT still shows them.
// `link`: the connection to the host (ping, loss), for the status line.
NavAction remote_play_screen_draw(const TouchUi* ui, const char* host, int32_t port, const PadLink* link,
                                  bool full_access, bool show_touch, const RemoteStream* stream, PadLayouts* layouts,
                                  PadEditor* editor);
