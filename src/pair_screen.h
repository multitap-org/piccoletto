#pragma once

// PAIR A DEVICE: pick a host found on the network (or type its address),
// then type the code it shows on its screen with the big keypad. The host
// decides what this phone may do (on its side).
// Portrait: all in one column; landscape: the keypad on the right.

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

constexpr int32_t PAIR_CODE_LENGTH = 6;

struct PairScreen
{
    int32_t selected = 0;                         // in the found list
    char    code[PAIR_CODE_LENGTH + 1] = {};      // digits typed so far
    int32_t length = 0;
};

// Clears the code (call when the screen opens).
void pair_screen_reset(PairScreen* pair);

// `found`: the names of the unpaired hosts on the network. *done is set when
// OK was tapped with a host and the whole code (the app sends the request).
NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const char* const* found, int32_t found_count, bool* done);
