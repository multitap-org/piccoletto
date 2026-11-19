#include "remote_play_screen.h"

#include <cmath>
#include <cstdio>

#include "pad_layout.h"
#include "pixel_font_imgui.h"

// The game's picture until video arrives: a SNES-sized frame (8:7 on screen).
constexpr float GAME_WIDTH  = 256.0f;
constexpr float GAME_HEIGHT = 224.0f;
constexpr float GAME_ASPECT = 8.0f / 7.0f;
// Between the bezel's edge and the picture, in retro pixels.
constexpr float BEZEL_MARGIN = 6.0f;
// The line under the picture (the host, the latency), in retro pixels.
constexpr float BEZEL_STATUS = 12.0f;

// Portrait (390 x 844 dp; the bezel is above y = 356).
static const PadPart PORTRAIT_PARTS[] = {
    {"##l", "L", 24, 432, 120, 40, PadPartKind::button, TouchText::normal},
    {"##r", "R", 246, 432, 120, 40, PadPartKind::button, TouchText::normal},
    {"##dpad_center", "", 70, 556, 52, 52, PadPartKind::button, TouchText::normal},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 70, 504, 52, 52, PadPartKind::button, TouchText::normal},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 70, 608, 52, 52, PadPartKind::button, TouchText::normal},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 18, 556, 52, 52, PadPartKind::button, TouchText::normal},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 122, 556, 52, 52, PadPartKind::button, TouchText::normal},
    {"##y", "Y", 214, 498, 52, 52, PadPartKind::button, TouchText::big},
    {"##x", "X", 314, 478, 52, 52, PadPartKind::button, TouchText::big},
    {"##b", "B", 214, 582, 68, 68, PadPartKind::button, TouchText::big},
    {"##a", "A", 298, 534, 68, 68, PadPartKind::button, TouchText::big},
    {"##select", "SELECT", 104, 700, 80, 32, PadPartKind::button, TouchText::normal},
    {"##start", "START", 196, 700, 80, 32, PadPartKind::button, TouchText::normal},
};
constexpr int32_t PORTRAIT_PART_COUNT = static_cast<int32_t>(sizeof(PORTRAIT_PARTS) / sizeof(PORTRAIT_PARTS[0]));

// Landscape (844 x 390 dp; the bezel is x = 221 .. 623).
static const PadPart LANDSCAPE_PARTS[] = {
    {"##l", "L", 32, 10, 160, 40, PadPartKind::button, TouchText::normal},
    {"##r", "R", 652, 10, 160, 40, PadPartKind::button, TouchText::normal},
    {"##dpad_center", "", 84, 154, 52, 52, PadPartKind::button, TouchText::normal},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 84, 102, 52, 52, PadPartKind::button, TouchText::normal},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 84, 206, 52, 52, PadPartKind::button, TouchText::normal},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 32, 154, 52, 52, PadPartKind::button, TouchText::normal},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 136, 154, 52, 52, PadPartKind::button, TouchText::normal},
    {"##select", "SELECT", 24, 300, 84, 40, PadPartKind::button, TouchText::normal},
    {"##start", "START", 116, 300, 84, 40, PadPartKind::button, TouchText::normal},
    {"##y", "Y", 650, 136, 52, 52, PadPartKind::button, TouchText::big},
    {"##x", "X", 752, 88, 52, 52, PadPartKind::button, TouchText::big},
    {"##b", "B", 650, 196, 64, 64, PadPartKind::button, TouchText::big},
    {"##a", "A", 740, 150, 64, 64, PadPartKind::button, TouchText::big},
};
constexpr int32_t LANDSCAPE_PART_COUNT = static_cast<int32_t>(sizeof(LANDSCAPE_PARTS) / sizeof(LANDSCAPE_PARTS[0]));

// The picture inside the bezel (x, y, width, height in retro pixels): 8:7, as
// big as fits above the status line; and the status line under it.
static void draw_bezel_contents(const TouchUi* ui, const char* host, float x, float y, float width, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    float room_width = width - 2.0f * BEZEL_MARGIN;
    float room_height = height - 2.0f * BEZEL_MARGIN - BEZEL_STATUS;
    float picture_width = room_width;
    float picture_height = std::floor(picture_width / GAME_ASPECT);
    if (picture_height > room_height)
    {
        picture_height = room_height;
        picture_width = std::floor(picture_height * GAME_ASPECT);
    }
    float picture_x = std::floor(x + (width - picture_width) * 0.5f);
    float picture_y = y + BEZEL_MARGIN;
    touch_ui_rect(ui, picture_x, picture_y, picture_width, picture_height, palette->field_face);

    char size[48];
    std::snprintf(size, sizeof(size), "%d×%d", static_cast<int>(GAME_WIDTH), static_cast<int>(GAME_HEIGHT));
    const char* label = "GAME VIDEO";
    float line = touch_ui_text_height(ui, TouchText::normal);
    float middle = picture_y + picture_height * 0.5f;
    touch_ui_text(ui, std::floor(picture_x + (picture_width - touch_ui_text_width(ui, label, TouchText::normal)) * 0.5f),
                  std::floor(middle - line), label, palette->text_disabled, TouchText::normal);
    touch_ui_text(ui, std::floor(picture_x + (picture_width - touch_ui_text_width(ui, size, TouchText::normal)) * 0.5f),
                  std::floor(middle + 2.0f), size, palette->text_disabled, TouchText::normal);

    float status_y = picture_y + picture_height + 3.0f;
    touch_ui_lamp(ui, picture_x, status_y, TOUCH_UI_LAMP_ONLINE);
    touch_ui_text(ui, picture_x + TOUCH_UI_LAMP + 5.0f, status_y, host, palette->text_disabled, TouchText::normal);
    const char* latency = "9 MS";   // placeholder until there's a connection to measure
    touch_ui_text(ui, picture_x + picture_width - touch_ui_text_width(ui, latency, TouchText::normal), status_y, latency,
                  palette->text_disabled, TouchText::normal);
}

NavAction remote_play_screen_draw(const TouchUi* ui, const char* host)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;

    // The face: the whole screen in the button color.
    touch_ui_rect(ui, 0.0f, 0.0f, width, height, palette->button_face);

    if (!landscape)
    {
        PadFrame frame = pad_frame_fit(ui, PAD_PORTRAIT_WIDTH, PAD_PORTRAIT_HEIGHT);
        // The bezel: flush with the top and both sides, down to the layout's 356 dp.
        float bezel_bottom = pad_y(&frame, 356);
        touch_ui_rect(ui, 0.0f, 0.0f, width, bezel_bottom, palette->panel_dark);
        draw_bezel_contents(ui, host, 0.0f, 0.0f, width, bezel_bottom);
        pad_text(ui, &frame, 24, 386, "piccoletto", palette->panel_dark, TouchText::big, false);
        pad_parts_draw(ui, &frame, PORTRAIT_PARTS, PORTRAIT_PART_COUNT);
        if (pad_button(ui, &frame, "##menu", 24, 772, 48, 48, PIXEL_FONT_UTF8_MENU, TouchButton::field, TouchText::big))
        {
            action = NavAction::menu;
        }
        if (pad_button(ui, &frame, "##rotate", 80, 772, 48, 48, PIXEL_FONT_UTF8_ROTATE, TouchButton::field, TouchText::big))
        {
            action = NavAction::rotate;
        }
        if (pad_button(ui, &frame, "##gamepad", 136, 772, 120, 48, "GAMEPAD", TouchButton::field, TouchText::normal))
        {
            action = NavAction::gamepad;
        }
        return action;
    }

    PadFrame frame = pad_frame_fit(ui, PAD_LANDSCAPE_WIDTH, PAD_LANDSCAPE_HEIGHT);
    // The bezel: flush with the top and bottom, between the two sides' controls.
    float bezel_left = pad_x(&frame, 221);
    float bezel_right = pad_x(&frame, 623);
    touch_ui_rect(ui, bezel_left, 0.0f, bezel_right - bezel_left, height, palette->panel_dark);
    draw_bezel_contents(ui, host, bezel_left, 0.0f, bezel_right - bezel_left, height);
    pad_text(ui, &frame, 32, 66, "piccoletto", palette->panel_dark, TouchText::big, false);
    pad_parts_draw(ui, &frame, LANDSCAPE_PARTS, LANDSCAPE_PART_COUNT);
    if (pad_button(ui, &frame, "##menu", 650, 300, 48, 48, PIXEL_FONT_UTF8_MENU, TouchButton::field, TouchText::big))
    {
        action = NavAction::menu;
    }
    if (pad_button(ui, &frame, "##rotate", 706, 300, 48, 48, PIXEL_FONT_UTF8_ROTATE, TouchButton::field, TouchText::big))
    {
        action = NavAction::rotate;
    }
    return action;
}
