#pragma once

// HOME: the paired hosts (piccolos). The selected one is a big card with the
// modes this phone may use on it; the others are one-line rows; at the
// bottom, PAIR A DEVICE with how many unpaired ones are on the network.
// Portrait and landscape (card on the left, rows on the right).

#include <cstdint>

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

enum class HomeAction
{
    none,
    gamepad,
    remote_play,
    devices,
    pair,
    menu,
};

struct HomeScreen
{
    int32_t selected = 0;   // the host on the big card
};

// Draws HOME between touch_ui_begin_screen / end. Returns what was tapped
// (for the selected host, home->selected).
HomeAction home_screen_draw(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count,
                            int32_t found_count);
