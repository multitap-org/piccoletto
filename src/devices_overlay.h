#pragma once

// DEVICES (Andrés: works in both GAMEPAD and REMOTE PLAY, drawn on top with
// low alpha): the host's ports and who plays on each (its own gamepads,
// other phones' pads, this phone's), over the game, which shows through. A
// phone with FULL access opens it from the ☰ sheet. Tap a device, then MOVE
// HERE on another port: the host moves it (device_port) and sends the new
// list. Landscape: the four ports side by side; portrait: two by two.
// The list is the host's (hosts->devices): only controllers that joined a
// port (START joins), nothing kept between runs.

#include <cstdint>
#include <vector>

#include "net_settings.h"
#include "screens.h"
#include "touch_ui.h"

constexpr int32_t DEVICES_PORTS = 4;

struct DevicesOverlay
{
    // The device being moved, by the host's names for it (they hold until
    // the next list; a new list keeps it picked if it's still there).
    bool     picked = false;
    uint32_t picked_controller = 0;
    uint32_t picked_device = 0;
};

// What a tap asked for.
struct DevicesMove
{
    bool    move = false;     // put `device` on `port`
    int32_t index = -1;       // into the list
    int8_t  port = -1;
};

// `known`: the host sent its list. Returns NavAction::devices when it's to
// close (DONE, a tap outside the ports); `move` says when a device is to move.
NavAction devices_overlay_draw(DevicesOverlay* overlay, const TouchUi* ui, const std::vector<NetDevice>* devices,
                               bool known, DevicesMove* move);
