#include "remote_play_screen.h"

#include <cmath>
#include <cstdio>

#include "pixel_font_imgui.h"
#include "quick_menu.h"
#include "stream_view.h"
#include "title_bar.h"

// The line under the picture in portrait (the port LEDs, the host, the latency), retro pixels.
constexpr float STATUS_HEIGHT = 12.0f;
// Under this the pad below the picture would be too small for fingers: it
// becomes an overlay instead (retro pixels per dp; the mockups' size is 0.5).
constexpr float MIN_BELOW_SCALE = 0.3f;

// The picture's place: the stream's size and aspect until there's video.
static void draw_picture(const TouchUi* ui, const RemoteStream* stream, float x, float y, float width, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, height, palette->field_face);
    if (stream->view != nullptr && stream->shell != nullptr &&
        stream_view_draw(stream->view, ui, stream->shell, x, y, width, height))
    {
        return;
    }
    char size[48];
    std::snprintf(size, sizeof(size), "%d×%d · %s", static_cast<int>(stream->width), static_cast<int>(stream->height),
                  stream->aspect_label);
    const char* label = stream->status != nullptr ? stream->status : "GAME VIDEO";
    float line = touch_ui_text_height(ui, TouchText::normal);
    float middle = y + height * 0.5f;
    touch_ui_text(ui, std::floor(x + (width - touch_ui_text_width(ui, label, TouchText::normal)) * 0.5f), std::floor(middle - line),
                  label, palette->text_disabled, TouchText::normal);
    touch_ui_text(ui, std::floor(x + (width - touch_ui_text_width(ui, size, TouchText::normal)) * 0.5f), std::floor(middle + 2.0f),
                  size, palette->text_disabled, TouchText::normal);
}

// The biggest picture of `aspect` inside the room, centered.
static void fit_picture(float aspect, float room_x, float room_y, float room_width, float room_height, float* x, float* y,
                        float* width, float* height)
{
    *width = room_width;
    *height = std::floor(room_width / aspect);
    if (*height > room_height)
    {
        *height = room_height;
        *width = std::floor(room_height * aspect);
    }
    *x = std::floor(room_x + (room_width - *width) * 0.5f);
    *y = std::floor(room_y + (room_height - *height) * 0.5f);
}

// The layout played, or edited.
static NavAction draw_pad(const TouchUi* ui, const PadFrame* frame, PadLayout* layout, PadEditor* editor, bool hotkeys,
                          bool show_touch)
{
    if (editor->active)
    {
        return pad_editor_draw(editor, ui, frame, layout) ? NavAction::edit_done : NavAction::none;
    }
    if (show_touch)
    {
        pad_layout_draw(ui, frame, layout, hotkeys);
    }
    return NavAction::none;
}

// Over the picture: no title bar, only the quick menu's see-through arrow
// at the top right; the port LEDs and the ping in the bottom left corner.
static NavAction draw_overlay_controls(const TouchUi* ui, int32_t port, const PadLink* link)
{
    title_bar_pad_status(ui, port, link, ui->palette.text_highlight);
    return quick_menu_arrow(ui) ? NavAction::menu : NavAction::none;
}

NavAction remote_play_screen_draw(const TouchUi* ui, const char* host, int32_t port, const PadLink* link,
                                  bool hotkeys, bool show_touch, const RemoteStream* stream, PadLayouts* layouts,
                                  PadEditor* editor)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    float px = 0.0f;
    float py = 0.0f;
    float pw = 0.0f;
    float ph = 0.0f;

    if (!landscape)
    {
        // The picture across the top; does a pad still fit under it?
        fit_picture(stream->aspect, 0.0f, 0.0f, width, height, &px, &py, &pw, &ph);
        PadLayout* below = pad_layouts_get(layouts, PadLayoutId::remote_below);
        float pad_top = ph + STATUS_HEIGHT;
        float pad_room = height - pad_top;
        PadFrame frame = pad_frame_fit_into(below->width, below->height, 0.0f, pad_top, width, pad_room > 0.0f ? pad_room : 0.0f);
        if (frame.dp_scale >= MIN_BELOW_SCALE)
        {
            touch_ui_rect(ui, 0.0f, 0.0f, width, height, palette->button_face);
            touch_ui_rect(ui, 0.0f, 0.0f, width, pad_top, palette->panel_dark);
            draw_picture(ui, stream, 0.0f, 0.0f, pw, ph);
            float status_y = ph + 2.0f;
            float leds = title_bar_port_leds(ui, 4.0f, status_y, port);
            touch_ui_text(ui, 4.0f + leds + 6.0f, status_y, host, palette->text_disabled, TouchText::normal);
            // The ping at the right.
            float ping_width = title_bar_ping_width(ui, link);
            title_bar_ping(ui, width - 4.0f - ping_width, status_y, link, palette->text_disabled);
            if (!editor->active && quick_menu_arrow(ui))
            {
                action = NavAction::menu;   // (over the picture's top right corner)
            }
            NavAction pad = draw_pad(ui, &frame, below, editor, hotkeys, show_touch);
            return pad != NavAction::none ? pad : action;
        }

        // A tall picture (the DS): as big as it gets, the pad over its lower part.
        draw_picture(ui, stream, px, py, pw, ph);
        PadLayout* overlay = pad_layouts_get(layouts, PadLayoutId::remote_overlay_portrait);
        PadFrame overlay_frame = pad_frame_fit(ui, overlay->width, overlay->height);
        NavAction icons = editor->active ? NavAction::none : draw_overlay_controls(ui, port, link);
        NavAction pad = draw_pad(ui, &overlay_frame, overlay, editor, hotkeys, show_touch);
        return pad != NavAction::none ? pad : icons;
    }

    // Landscape: the picture as tall as the screen, the pad over it.
    fit_picture(stream->aspect, 0.0f, 0.0f, width, height, &px, &py, &pw, &ph);
    draw_picture(ui, stream, px, py, pw, ph);
    PadLayout* overlay = pad_layouts_get(layouts, PadLayoutId::remote_overlay_landscape);
    PadFrame frame = pad_frame_fit_bottom(ui, overlay->width, overlay->height);
    NavAction icons = editor->active ? NavAction::none : draw_overlay_controls(ui, port, link);
    NavAction pad = draw_pad(ui, &frame, overlay, editor, hotkeys, show_touch);
    return pad != NavAction::none ? pad : icons;
}
