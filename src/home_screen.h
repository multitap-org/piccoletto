#pragma once

// HOME: the paired hosts (piccolos). The selected one is a big card with the
// modes this phone may use on it; the others are behind a sidebar, closed
// to a tab at the right edge in landscape and to a bar at the bottom (like a
// status bar) in portrait. Open, it shows one card per host (like DEVICES'
// ports), as big as they need up to 80% of the screen. PAIR A DEVICE is the
// title bar's + (with how many unpaired ones were found).

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

// What the host granted this phone when it was paired (decided on the host's
// side, never here).
struct HostAccess
{
    bool gamepad     = false;
    bool remote_play = false;
    bool devices     = false;   // may move the connected devices between ports
};

struct HomeHost
{
    const char* name    = "";
    bool        online  = false;
    const char* playing = "";   // the game running there, "" if none
    HostAccess  access  = {};
};

struct HomeScreen
{
    int32_t selected     = 0;       // the host on the big card
    bool    sidebar_open = false;   // the other hosts' sidebar
};

// Draws HOME between touch_ui_begin_screen / end. Returns what was tapped
// (for the selected host, home->selected).
NavAction home_screen_draw(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count,
                            int32_t found_count);

// The sidebar, in its own overlay window over HOME (touch_ui_begin_overlay),
// while home->sidebar_open: tapping a host puts it on the card, tapping
// outside closes it.
void home_sidebar_draw(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count);
