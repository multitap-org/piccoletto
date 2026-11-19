#pragma once

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
};

enum class Screen
{
    home,
    pair,
    gamepad,
    remote_play,
    devices,
};

// "GAMEPAD" etc., for logs (English on purpose).
const char* nav_action_name(NavAction action);
