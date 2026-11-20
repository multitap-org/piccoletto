#pragma once

#include <cstdint>

// What a screen asks the app to do when something on it is tapped.

enum class NavAction
{
    none,
    home,          // back to HOME
    menu,          // open the ☰ sheet
    rotate,        // turn the screen (portrait <-> landscape)
    pair,
    gamepad,
    remote_play,
    devices,
    edit_layout,   // EDIT LAYOUT on the current pad
    edit_done,     // EDIT LAYOUT's DONE: save the layouts
    settings,
    touch_controls,   // REMOTE PLAY with a real gamepad: show / hide the touch controls
    measure,          // HOME: speed-test the selected host now
};

enum class Screen
{
    home,
    pair,
    gamepad,
    remote_play,
    devices,
    settings,
};

// "GAMEPAD" etc., for logs (English on purpose).
const char* nav_action_name(NavAction action);

// What the pads' status shows of the connection to the host.
enum class LinkShown
{
    offline,
    connecting,
    ready,       // with a ping
};

struct PadLink
{
    LinkShown state   = LinkShown::offline;
    uint32_t  ping_ms = 0;      // round trip
    float     loss    = 0.0f;   // packets lost, 0..1
};
