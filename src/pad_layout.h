#pragma once

// The touch pad's parts (GAMEPAD, REMOTE PLAY): blocks like piccolo's pad
// picture (retroui's pad_picture), lit yellow while held. Layouts are tables
// in the mockups' dp (a 390 x 844 portrait phone, 844 x 390 landscape),
// scaled to fit the real screen and centered (see PadFrame).
//
// For now a part is "held" while the mouse (one finger) is on it; real
// multi-touch (SDL finger events) comes with the network input.

#include <cstdint>

#include "touch_ui.h"

enum class PadPartKind
{
    button,   // a block with its label
    stick,    // a block with a nub in the middle
};

struct PadPart
{
    const char* id;      // unique on the screen ("##a")
    const char* label;   // "" = none
    float       x, y, width, height;   // dp in the reference layout
    PadPartKind kind;
    TouchText   size;
};

// Reference dp -> the screen's retro pixels: x_retro = x + dp * dp_scale.
struct PadFrame
{
    float x;
    float y;
    float dp_scale;   // retro pixels per dp
};

// The reference sizes, in dp.
constexpr float PAD_PORTRAIT_WIDTH   = 390.0f;
constexpr float PAD_PORTRAIT_HEIGHT  = 844.0f;
constexpr float PAD_LANDSCAPE_WIDTH  = 844.0f;
constexpr float PAD_LANDSCAPE_HEIGHT = 390.0f;

// The frame that fits a `width` x `height` dp layout on the screen, centered.
PadFrame pad_frame_fit(const TouchUi* ui, float width, float height);

// A reference rectangle on the screen, in retro pixels.
float pad_x(const PadFrame* frame, float dp);
float pad_y(const PadFrame* frame, float dp);
float pad_size(const PadFrame* frame, float dp);

// Draws the parts; returns how many are held now (for the status line).
int32_t pad_parts_draw(const TouchUi* ui, const PadFrame* frame, const PadPart* parts, int32_t count);

// A flat block (the pad's body, a bezel) in reference dp.
void pad_block(const TouchUi* ui, const PadFrame* frame, float x, float y, float width, float height, uint32_t argb);

// A button in reference dp (touch_ui_button with the frame applied).
bool pad_button(const TouchUi* ui, const PadFrame* frame, const char* id, float x, float y, float width, float height,
                const char* label, TouchButton style, TouchText size);

// Text at reference dp (x, y); `centered`: x is the middle.
void pad_text(const TouchUi* ui, const PadFrame* frame, float x, float y, const char* text, uint32_t argb, TouchText size,
              bool centered);
