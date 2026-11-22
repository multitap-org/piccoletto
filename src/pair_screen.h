#pragma once

// PAIR (Andrés, 2026-11-22; docs/mockups/pairing/piccoletto-pairing-*.png):
// the phone shows its code, the piccolo's user types it.
//   1. pick: the piccolos on this network as cards, YOUR CODE big, PAIR
//      (primary, with a card picked) and INTERNET (the piccolo is elsewhere).
//   2. waiting: the same code, the four lamps lit one at a time, what to do on
//      the piccolo; CANCEL. Across the internet SEND CODE (Android's share
//      sheet) to get the code to whoever sits at the piccolo.
//   3. paired: the piccolo and what it gave this phone; CONTINUE to HOME.
// A failure goes back to 1 with a new code and says why.
// Portrait: one column; landscape: the cards left, the code and buttons right.

#include <cstdint>

#include "net_pairing.h"
#include "screens.h"
#include "touch_ui.h"

enum class PairStep
{
    pick,
    waiting,
    paired,
};

enum class PairTap
{
    none,
    pair,        // PAIR: with found[selected]
    internet,    // INTERNET: wait for the piccolo's INVITE
    cancel,      // CANCEL while waiting
    send_code,   // SEND CODE (across the internet)
    proceed,     // CONTINUE once paired
};

struct PairScreen
{
    int32_t selected = 0;                         // in the found list
    char    code[NET_PAIR_CODE_DIGITS + 1] = {};  // ours, shown; made when the screen opens
};

// What the app knows, to draw.
struct PairView
{
    PairStep    step = PairStep::pick;
    const char* const* found = nullptr;    // the unpaired piccolos on this network
    int32_t     found_count = 0;
    bool        internet = false;          // waiting: across the internet (else the picked card)
    bool        host_asked = false;        // waiting: the host has our request
    const char* error = "";                // pick: why the last try failed ("" = none)
    // paired
    const char* host_name = "";
    bool        remote_play = false;       // it gave STREAM or more
    bool        devices = false;           // FULL
};

// A new code (call when the screen opens, and after a failure).
void pair_screen_reset(PairScreen* pair);

// `tap` gets what was tapped this frame (PairTap::none if nothing).
NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const PairView* view, PairTap* tap);
