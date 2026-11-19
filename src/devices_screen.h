#pragma once

// DEVICES: only for a phone the host allowed to manage (decided when it was
// paired). The host's ports as cards, each with the devices playing in it
// (phones, pads); tap a device, then MOVE HERE on another port.
// Portrait: the ports one under another; landscape: side by side.
// Placeholder devices until the host sends its list.

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

constexpr int32_t DEVICES_PORTS = 4;
constexpr int32_t DEVICES_MAX   = 8;

struct DevicesScreen
{
    const char* names[DEVICES_MAX] = {};
    int32_t     port[DEVICES_MAX]  = {};   // 0-based
    int32_t     count    = 0;
    int32_t     selected = -1;              // the device being moved, -1 = none
};

// The placeholder devices (the mockups' example: 4 phones and a pad).
void devices_screen_init(DevicesScreen* devices);

NavAction devices_screen_draw(DevicesScreen* devices, const TouchUi* ui, const char* host);
