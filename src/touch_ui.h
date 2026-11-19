#pragma once

// piccoletto's touch UI: the "retro but modern" look (Andrés, 2026-11-19):
// flat cards on the background, big touch targets, retroui's pixel font and
// palettes, none of piccolo's desktop chrome (menu bar, title bars, bevels).
//
// Everything is laid out in retro pixels (one pixel of the 5x5 font) times
// `scale` window pixels, so edges stay on whole pixels and the font stays
// crisp. Text comes in two sizes: normal (1 retro pixel per font pixel) and
// big (1.5, rounded down to whole window pixels).
//
// These widgets start here; once they settle (or piccolo wants them) they
// move to retroui.

#include <cstdint>

#include "retro_ui.h"

struct ImFont;

struct TouchUi
{
    ImFont*        font  = nullptr;
    int32_t        scale = 1;          // window pixels per retro pixel
    RetroUiPalette palette = {};
};

// The scale for a window: the short side holds at least 195 retro pixels
// (a phone's 390 dp at 2 dp per retro pixel). At least 1.
int32_t touch_ui_scale_for(float width, float height);

// A screen: one undecorated window over the whole display, in the
// palette's background color. Draw with the functions below in between.
// Sizes and positions below are in retro pixels from the screen's top left.
void touch_ui_begin_screen(const TouchUi* ui, const char* id);
void touch_ui_end_screen();

// An overlay (a sheet over a screen): a transparent window over the whole
// display, above the screen, so the screen behind takes no taps while it's
// up. Close with touch_ui_end_screen().
void touch_ui_begin_overlay(const TouchUi* ui, const char* id);

// The screen's size in retro pixels.
float touch_ui_width(const TouchUi* ui);
float touch_ui_height(const TouchUi* ui);

enum class TouchText
{
    normal,
    big,
};

// Text with its top left at (x, y). Returns its width in retro pixels.
float touch_ui_text(const TouchUi* ui, float x, float y, const char* text, uint32_t argb, TouchText size);
float touch_ui_text_width(const TouchUi* ui, const char* text, TouchText size);
// The line's height in retro pixels.
float touch_ui_text_height(const TouchUi* ui, TouchText size);

// A filled rectangle.
void touch_ui_rect(const TouchUi* ui, float x, float y, float width, float height, uint32_t argb);

enum class TouchButton
{
    normal,    // the button face
    primary,   // the highlight color: the screen's main action
    ghost,     // only an outline
    card,      // a card you can tap (a row in a list): no lip
    plain,     // only the label (an icon in a header)
    field,     // a pad part's colors (on a pad's body): no lip
};

// A button you can tap: the label centered (or, for `card`, at the left).
// Normal and primary buttons have a lip at the bottom that goes away while
// pressed. True when tapped. `id` must be unique on the screen.
bool touch_ui_button(const TouchUi* ui, const char* id, float x, float y, float width, float height,
                     const char* label, TouchButton style, TouchText size);

// A touch area with nothing drawn (the caller draws it): true when tapped,
// *held while a finger (the mouse) is on it.
bool touch_ui_area(const TouchUi* ui, const char* id, float x, float y, float width, float height, bool* held);

// Where the finger (the mouse) is, in retro pixels from the screen's top left.
void touch_ui_pointer(const TouchUi* ui, float* x, float* y);

// A light the height of a letter at (x, y + 2): online green, or `argb`.
constexpr float TOUCH_UI_LAMP = 5.0f;
constexpr uint32_t TOUCH_UI_LAMP_ONLINE = 0xFF3AD15Au;
void touch_ui_lamp(const TouchUi* ui, float x, float y, uint32_t argb);

// Where text of `size` goes to look centered in a box `height` tall at `y`.
float touch_ui_centered_text_y(const TouchUi* ui, float y, float height, TouchText size);

// Only what's inside (x, y, width, height) is drawn and takes taps until the
// pop (a scrolling list under a fixed header).
void touch_ui_push_clip(const TouchUi* ui, float x, float y, float width, float height);
void touch_ui_pop_clip();
