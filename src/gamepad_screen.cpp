#include "gamepad_screen.h"

#include <cstdio>

#include "pixel_font_imgui.h"
#include "title_bar.h"

// The pad's layout (or its editor, while editing).
static NavAction draw_pad(const TouchUi* ui, const PadFrame* frame, PadLayout* layout, PadEditor* editor, bool full_access)
{
    if (editor->active)
    {
        return pad_editor_draw(editor, ui, frame, layout) ? NavAction::edit_done : NavAction::none;
    }
    pad_layout_draw(ui, frame, layout, full_access);
    return NavAction::none;
}

NavAction gamepad_screen_draw(const TouchUi* ui, const char* host, int32_t port, LinkShown link, uint32_t ping_ms,
                              bool full_access, PadLayouts* layouts, PadEditor* editor)
{
    const RetroUiPalette* palette = &ui->palette;
    bool landscape = touch_ui_width(ui) > touch_ui_height(ui);
    NavAction action = NavAction::none;

    if (!landscape)
    {
        PadLayout* layout = pad_layouts_get(layouts, PadLayoutId::gamepad_portrait);
        PadFrame frame = pad_frame_fit(ui, layout->width, layout->height);
        pad_block(ui, &frame, 16, 196, 358, 632, palette->button_face);
        if (!editor->active)
        {
            // Header: back, the host (online light), rotate, the sheet.
            if (pad_button(ui, &frame, "##back", 12, 16, 48, 48, PIXEL_FONT_UTF8_ARROW_LEFT, TouchButton::plain, TouchText::big))
            {
                action = NavAction::home;
            }
            float name_y = touch_ui_centered_text_y(ui, pad_y(&frame, 16), pad_size(&frame, 48), TouchText::normal);
            touch_ui_lamp(ui, pad_x(&frame, 68), name_y, TOUCH_UI_LAMP_ONLINE);
            touch_ui_text(ui, pad_x(&frame, 68) + TOUCH_UI_LAMP + 5.0f, name_y, host, palette->text_highlight, TouchText::normal);
            if (pad_button(ui, &frame, "##rotate", 282, 16, 48, 48, PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
            {
                action = NavAction::rotate;
            }
            if (pad_button(ui, &frame, "##menu", 330, 16, 48, 48, PIXEL_FONT_UTF8_MENU, TouchButton::plain, TouchText::big))
            {
                action = NavAction::menu;
            }
        }
        NavAction pad = draw_pad(ui, &frame, layout, editor, full_access);
        return pad != NavAction::none ? pad : action;
    }

    // Landscape: the pad's body is the whole screen, the layout as big as it fits.
    touch_ui_rect(ui, 0.0f, 0.0f, touch_ui_width(ui), touch_ui_height(ui), palette->button_face);
    PadLayout* layout = pad_layouts_get(layouts, PadLayoutId::gamepad_landscape);
    PadFrame frame = pad_frame_fit_bottom(ui, layout->width, layout->height);
    if (!editor->active)
    {
        // The title bar's back arrow and icons where HOME has them (+ off here).
        action = title_bar_icons(ui, false, 0);
        if (title_bar_back(ui))
        {
            action = NavAction::home;
        }
        title_bar_host(ui, host, palette->text_highlight);
        title_bar_pad_status(ui, port, link, ping_ms, palette->text_highlight);
    }
    NavAction pad = draw_pad(ui, &frame, layout, editor, full_access);
    return pad != NavAction::none ? pad : action;
}
