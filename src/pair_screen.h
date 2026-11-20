#pragma once

// PAIR A DEVICE: pick a host found on the network (or type its address),
// then type the code it shows on its screen with the big keypad. The host
// decides what this phone may do (on its side). While the app pairs, the
// screen shows how it goes (or why it failed) under the code.
// Portrait: all in one column; landscape: the keypad on the right.

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

constexpr int32_t PAIR_CODE_LENGTH = 6;

constexpr int32_t PAIR_ADDRESS_SIZE = 64;

struct PairScreen
{
    int32_t selected = 0;                         // in the found list
    char    code[PAIR_CODE_LENGTH + 1] = {};      // digits typed so far
    int32_t length = 0;
    bool    typing_address = false;               // the address field instead of a found host
    char    address[PAIR_ADDRESS_SIZE] = {};      // "192.168.1.20" or "192.168.1.20:55420"
    bool    typed = false;                        // a key was tapped this frame (the app clears an old error)
};

// How the pairing goes, from the app.
struct PairProgress
{
    bool        busy  = false;     // connecting / checking: the keypad waits
    const char* text  = "";        // "CONNECTING…", "WRONG CODE"...
    bool        error = false;     // `text` is a failure (in the highlight color)
};

// Clears the code (call when the screen opens).
void pair_screen_reset(PairScreen* pair);

// `found`: the names of the unpaired hosts on the network. *done is set when
// OK was tapped with a host (pair->selected, or pair->address while
// pair->typing_address) and the whole code: the app starts pairing.
NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const char* const* found, int32_t found_count,
                           const PairProgress* progress, bool* done);
