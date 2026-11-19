#pragma once

// The title bar's icons, top right, in the same place on HOME, GAMEPAD and
// REMOTE PLAY (Andrés, 2026-11-19): + (PAIR A DEVICE, with how many
// unpaired devices were found), rotate, ☰. On the pads + is shown but off.
// The left side is the screen's: the name on HOME (a logo later), the back
// arrow on every other screen (Andrés: leaving never needs the ☰).

#include <cstdint>

#include "screens.h"
#include "touch_ui.h"

constexpr float TITLE_BAR_MARGIN = 8.0f;    // retro pixels from the screen's edges
constexpr float TITLE_BAR_ICON   = 24.0f;   // each icon's square
// Its top: 6 retro pixels in landscape (phones have little height then), 8 in portrait.
float title_bar_y(const TouchUi* ui);
// Where the screen's content can start under the title bar.
float title_bar_bottom(const TouchUi* ui);
// The left edge of the icons (the screen's own title must end before it).
float title_bar_icons_x(const TouchUi* ui);

// Draws the three icons. `pair_enabled` false: + is drawn off and takes no
// taps. `found_count` > 0 puts that number by the +.
NavAction title_bar_icons(const TouchUi* ui, bool pair_enabled, int32_t found_count);

// The back arrow at the title bar's left: true when tapped.
bool title_bar_back(const TouchUi* ui);

// Four LEDs, the port's one lit (ports 1..4), like a console pad's player
// lights. (x, y) is the top left; returns their width.
constexpr int32_t TITLE_BAR_PORTS = 4;
float title_bar_port_leds(const TouchUi* ui, float x, float y, int32_t port);

// The pads' title: the host in the middle of the title bar's row.
void title_bar_host(const TouchUi* ui, const char* host, uint32_t text_argb);

// The ping as three bars (Andrés): under 16 ms three green, under 48 ms two
// yellow, else one red; all dim while connecting or offline. Then the ms
// ("20 MS"), CONNECTING or OFFLINE. Returns the width.
float title_bar_ping(const TouchUi* ui, float x, float y, LinkShown link, uint32_t ping_ms, uint32_t text_argb);
float title_bar_ping_width(const TouchUi* ui, LinkShown link, uint32_t ping_ms);

// The pads' status in the screen's bottom left corner: the port LEDs and the ping.
void title_bar_pad_status(const TouchUi* ui, int32_t port, LinkShown link, uint32_t ping_ms, uint32_t text_argb);
