#pragma once

// PAIR (Andrés, 2026-11-25; the pairing v3 page): the same two modes as
// piccolo's PAIR..., in piccoletto's look. The device asked to pair has the
// code (it grants the permissions); piccoletto offers nothing, so it never
// ticks: it only shows what it was given.
//
// PAIR A DEVICE
//   1. list: DISCOVER DEVICES (those offering something the phone uses) as
//      cards, then ENTER A CODE (a device elsewhere shows one); PAIR.
//   2. typing: TYPE THE CODE SHOWN ON <NAME> (on this network the device has
//      popped it up; WAITING FOR <NAME> until it has), nine boxes and a
//      number pad; PAIR, CANCEL.
//   3. done: the device, PERMISSIONS GRANTED ON <NAME>; DONE goes home.
// GENERATE PAIRING CODE (across the internet: a piccolo types it in its
// ENTER A CODE)
//   1. TYPE THIS CODE ON THE OTHER DEVICE, the code, WAITING FOR THE OTHER
//      DEVICE · 5 MIN; SEND CODE (Android's share sheet), CANCEL.
//   2. the device typed it: TYPED YOUR CODE, WAITING FOR <NAME> (it ticks last).
//   3. paired: as PAIR A DEVICE's.
// A failure goes back to the start of the mode and says why. Portrait: one
// column; landscape: what to read left, the pad / status and buttons right.

#include <cstdint>

#include "net_pairing.h"
#include "screens.h"
#include "touch_ui.h"

enum class PairMode
{
    device,   // PAIR A DEVICE
    code,     // GENERATE PAIRING CODE
};

enum class PairStep
{
    list,
    typing,
    done,
};

// GENERATE PAIRING CODE's steps, as the app follows them (hosts' CodeStatus).
enum class PairCodeStep
{
    starting,   // nothing shown yet (or it failed: `error`)
    showing,
    chosen,
    done,
};

enum class PairTap
{
    none,
    mode,        // a mode's tab: PairScreen::mode is the new one
    pair,        // PAIR on the list: found[selected], or ENTER A CODE (selected == found_count)
    submit,      // PAIR with nine digits typed
    cancel,      // CANCEL / back to the start of the mode
    send_code,   // SEND CODE
    retry,       // TRY AGAIN (GENERATE PAIRING CODE failed)
    done,        // DONE: home
};

struct PairScreen
{
    PairMode mode = PairMode::device;
    int32_t  selected = -1;                        // in the list; found_count = ENTER A CODE
    char     typed[NET_PAIR_CODE_DIGITS + 1] = {}; // the code being typed
};

// What the app knows, to draw.
struct PairView
{
    PairStep     step = PairStep::list;
    PairCodeStep code_step = PairCodeStep::starting;
    const char* const* found = nullptr;      // the pairable devices on this network
    const char* const* found_kinds = nullptr;   // ... their kinds (PICCOLO, PICCOLINO)
    int32_t      found_count = 0;
    bool         busy_device = false;        // PAIR A DEVICE is past its list (GENERATE PAIRING CODE greyed)
    bool         busy_code = false;          // a code is up or being answered (PAIR A DEVICE greyed)
    const char*  error = "";                 // why the last try failed ("" = none)
    // typing
    const char*  host_name = "";             // "" = THE OTHER DEVICE (ENTER A CODE)
    bool         code_ready = false;         // the device shows its code (on this network), or ENTER A CODE
    bool         checking = false;           // the typed code is being checked
    bool         wrong = false;              // the last code was wrong: the device shows a new one
    // GENERATE PAIRING CODE
    const char*  shown_code = "";            // ours
    // done (both modes) and chosen
    const char*  done_name = "";
    const char*  done_kind = "";
    const char*  done_granted = "";          // "GAMEPAD, REMOTE PLAY" or "-"
};

// Back to a mode's start (the typed code forgotten).
void pair_screen_reset(PairScreen* pair);

// `tap` gets what was tapped this frame (PairTap::none if nothing).
NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const PairView* view, PairTap* tap);
