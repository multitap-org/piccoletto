#pragma once

// Who plays on the host's ports, drawn with retroui's ports_view (the same
// as piccolo's; Andrés, 2026-11-20). Replaces the old DEVICES overlay.
//
//   TABS   over GAMEPAD and REMOTE PLAY, at the bottom left, every access
//          level (read only: the host's list comes with devices_watch).
//          When the list changes, the tabs rise (the changed ports whole; a
//          device that left greyed on its old port) and go down ~3 s later.
//   BOARD  DEVICES (SETTINGS granted, from the ☰ sheet): tap a device to pick it,
//          tap another port to move it there (device_port), DONE closes.
//
// The list is the host's (hosts->devices): only controllers that joined a
// port. This phone's pads are marked, with this link's ping.

#include <cstdint>
#include <vector>

#include "net_settings.h"
#include "ports_view.h"
#include "screens.h"
#include "touch_ui.h"

constexpr uint32_t PORTS_OVERLAY_PORTS = 4;   // the host's ports (the list doesn't say how many)

struct PortsOverlay
{
    PortsView              view;
    bool                   known = false;   // a list arrived (the summaries are taken)
    uint64_t               summary[PORTS_OVERLAY_PORTS] = {};
    std::vector<NetDevice> last;            // the list last seen (to grey who left)
};

// What a tap on the board asked for.
struct PortsOverlayMove
{
    bool    move = false;   // put devices[index] on `port`
    int32_t index = -1;
    int8_t  port = -1;
};

// Every frame in GAMEPAD / REMOTE PLAY: the host's list (`known`: it came),
// this link (its ping on this phone's pads).
void ports_overlay_update(PortsOverlay* overlay, const std::vector<NetDevice>* devices, bool known, const PadLink* link,
                          uint64_t now_ms);

// Nothing shown, nothing remembered (out of the modes, or offline).
void ports_overlay_reset(PortsOverlay* overlay);

// The tabs, in front of the screen; `hidden` while something covers it.
void ports_overlay_draw_tabs(PortsOverlay* overlay, const TouchUi* ui, bool hidden, uint64_t now_ms);

// DEVICES. Returns NavAction::devices when DONE closes it.
NavAction ports_overlay_draw_board(PortsOverlay* overlay, const TouchUi* ui, uint64_t now_ms, const std::vector<NetDevice>* devices,
                                   bool known, PortsOverlayMove* move);
