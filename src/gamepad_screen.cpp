#include "gamepad_screen.h"

#include <cstdio>

#include "pad_layout.h"
#include "pixel_font_imgui.h"

// Portrait (390 x 844 dp), from the mockup: shoulders on top, the body below
// with the d-pad and the face buttons, SELECT / START / the piccolo menu,
// both sticks at the bottom.
static const PadPart PORTRAIT_PARTS[] = {
    {"##l2", "L2", 16, 92, 160, 40, PadPartKind::button, TouchText::normal},
    {"##r2", "R2", 214, 92, 160, 40, PadPartKind::button, TouchText::normal},
    {"##l", "L", 16, 140, 160, 44, PadPartKind::button, TouchText::normal},
    {"##r", "R", 214, 140, 160, 44, PadPartKind::button, TouchText::normal},
    {"##dpad_center", "", 76, 328, 56, 56, PadPartKind::button, TouchText::normal},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 76, 272, 56, 56, PadPartKind::button, TouchText::normal},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 76, 384, 56, 56, PadPartKind::button, TouchText::normal},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 20, 328, 56, 56, PadPartKind::button, TouchText::normal},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 132, 328, 56, 56, PadPartKind::button, TouchText::normal},
    {"##x", "X", 258, 272, 56, 56, PadPartKind::button, TouchText::big},
    {"##b", "B", 258, 384, 56, 56, PadPartKind::button, TouchText::big},
    {"##y", "Y", 202, 328, 56, 56, PadPartKind::button, TouchText::big},
    {"##a", "A", 314, 328, 56, 56, PadPartKind::button, TouchText::big},
    {"##select", "SELECT", 100, 496, 88, 44, PadPartKind::button, TouchText::normal},
    {"##start", "START", 202, 496, 88, 44, PadPartKind::button, TouchText::normal},
    {"##guide", PIXEL_FONT_UTF8_MENU, 167, 556, 56, 40, PadPartKind::button, TouchText::normal},
    {"##lstick", "", 48, 652, 112, 112, PadPartKind::stick, TouchText::normal},
    {"##rstick", "", 230, 652, 112, 112, PadPartKind::stick, TouchText::normal},
};
constexpr int32_t PORTRAIT_PART_COUNT = static_cast<int32_t>(sizeof(PORTRAIT_PARTS) / sizeof(PORTRAIT_PARTS[0]));

// Landscape (844 x 390 dp): piccolo's pad picture (100 x 46 units) at 8 dp
// a unit, a little bigger SELECT / START for fingers.
static const PadPart LANDSCAPE_PARTS[] = {
    {"##l2", "L2", 134, 8, 112, 32, PadPartKind::button, TouchText::normal},
    {"##r2", "R2", 598, 8, 112, 32, PadPartKind::button, TouchText::normal},
    {"##l", "L", 118, 48, 144, 32, PadPartKind::button, TouchText::normal},
    {"##r", "R", 582, 48, 144, 32, PadPartKind::button, TouchText::normal},
    {"##dpad_center", "", 174, 160, 48, 48, PadPartKind::button, TouchText::normal},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 174, 112, 48, 48, PadPartKind::button, TouchText::normal},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 174, 208, 48, 48, PadPartKind::button, TouchText::normal},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 126, 160, 48, 48, PadPartKind::button, TouchText::normal},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 222, 160, 48, 48, PadPartKind::button, TouchText::normal},
    {"##x", "X", 622, 112, 48, 48, PadPartKind::button, TouchText::big},
    {"##b", "B", 622, 208, 48, 48, PadPartKind::button, TouchText::big},
    {"##y", "Y", 566, 160, 48, 48, PadPartKind::button, TouchText::big},
    {"##a", "A", 678, 160, 48, 48, PadPartKind::button, TouchText::big},
    {"##select", "SELECT", 318, 132, 80, 36, PadPartKind::button, TouchText::normal},
    {"##start", "START", 446, 132, 80, 36, PadPartKind::button, TouchText::normal},
    {"##guide", PIXEL_FONT_UTF8_MENU, 398, 184, 48, 36, PadPartKind::button, TouchText::normal},
    {"##lstick", "", 278, 236, 72, 72, PadPartKind::stick, TouchText::normal},
    {"##rstick", "", 494, 236, 72, 72, PadPartKind::stick, TouchText::normal},
};
constexpr int32_t LANDSCAPE_PART_COUNT = static_cast<int32_t>(sizeof(LANDSCAPE_PARTS) / sizeof(LANDSCAPE_PARTS[0]));

NavAction gamepad_screen_draw(const TouchUi* ui, const char* host, int32_t port)
{
    const RetroUiPalette* palette = &ui->palette;
    bool landscape = touch_ui_width(ui) > touch_ui_height(ui);
    NavAction action = NavAction::none;

    if (!landscape)
    {
        PadFrame frame = pad_frame_fit(ui, PAD_PORTRAIT_WIDTH, PAD_PORTRAIT_HEIGHT);
        // Header: back, the host (online light), rotate, the sheet.
        if (pad_button(ui, &frame, "##back", 12, 16, 48, 48, PIXEL_FONT_UTF8_ARROW_LEFT, TouchButton::plain, TouchText::big))
        {
            action = NavAction::home;
        }
        float name_y = touch_ui_centered_text_y(ui, pad_y(&frame, 16), pad_size(&frame, 48), TouchText::normal);
        touch_ui_lamp(ui, pad_x(&frame, 68), name_y, TOUCH_UI_LAMP_ONLINE);
        // The header keeps to the host; the port shows in landscape's status line.
        touch_ui_text(ui, pad_x(&frame, 68) + TOUCH_UI_LAMP + 5.0f, name_y, host, palette->text_highlight, TouchText::normal);
        if (pad_button(ui, &frame, "##rotate", 282, 16, 48, 48, PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
        {
            action = NavAction::rotate;
        }
        if (pad_button(ui, &frame, "##menu", 330, 16, 48, 48, PIXEL_FONT_UTF8_MENU, TouchButton::plain, TouchText::big))
        {
            action = NavAction::menu;
        }
        pad_block(ui, &frame, 16, 196, 358, 632, palette->button_face);
        pad_parts_draw(ui, &frame, PORTRAIT_PARTS, PORTRAIT_PART_COUNT);
        return action;
    }

    PadFrame frame = pad_frame_fit(ui, PAD_LANDSCAPE_WIDTH, PAD_LANDSCAPE_HEIGHT);
    // Rotate and the sheet between the shoulders; no back button (the sheet's LEAVE).
    if (pad_button(ui, &frame, "##rotate", 370, 20, 48, 48, PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }
    if (pad_button(ui, &frame, "##menu", 426, 20, 48, 48, PIXEL_FONT_UTF8_MENU, TouchButton::plain, TouchText::big))
    {
        action = NavAction::menu;
    }
    pad_block(ui, &frame, 86, 88, 672, 272, palette->button_face);
    pad_parts_draw(ui, &frame, LANDSCAPE_PARTS, LANDSCAPE_PART_COUNT);
    char status[64];
    std::snprintf(status, sizeof(status), "%s · PORT %d", host, static_cast<int>(port));
    pad_text(ui, &frame, 422, 330, status, palette->panel_dark, TouchText::normal, true);
    return action;
}
