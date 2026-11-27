#include "gamepad_screen.h"

#include <cstdio>

#include "pixel_font_imgui.h"
#include "quick_menu.h"
#include "title_bar.h"

// The pad's layout (or its editor, while editing).
static NavAction draw_pad(const TouchUi* ui, const PadFrame* frame, PadLayout* layout, PadEditor* editor, bool hotkeys)
{
    layout->action_label = hotkeys ? "HOTKEY" : nullptr;   // (retroui's action button: piccoletto's HOTKEY)
    // retroui's menu button, between SELECT and START: the quick menu (as its arrow and holding START do).
    layout->menu_label = PIXEL_FONT_UTF8_MENU;
    if (editor->active)
    {
        return pad_editor_draw(editor, ui, frame, layout) ? NavAction::edit_done : NavAction::none;
    }
    pad_layout_draw(ui, frame, layout);
    return layout->menu_pressed ? NavAction::menu : NavAction::none;
}

NavAction gamepad_screen_draw(const TouchUi* ui, int32_t port, const PadLink* link,
                              bool hotkeys, PadLayouts* layouts, PadEditor* editor)
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
            // No title bar: the quick menu's arrow (the host is in the menu).
            title_bar_pad_status(ui, port, link, palette->text_highlight);
            if (quick_menu_arrow(ui))
            {
                action = NavAction::menu;
            }
        }
        NavAction pad = draw_pad(ui, &frame, layout, editor, hotkeys);
        return pad != NavAction::none ? pad : action;
    }

    // Landscape: the pad's body is the whole screen, the layout as big as it fits.
    touch_ui_rect(ui, 0.0f, 0.0f, touch_ui_width(ui), touch_ui_height(ui), palette->button_face);
    PadLayout* layout = pad_layouts_get(layouts, PadLayoutId::gamepad_landscape);
    PadFrame frame = pad_frame_fit_bottom(ui, layout->width, layout->height);
    if (!editor->active)
    {
        // No title bar: the quick menu's arrow; the port and the ping at the bottom left.
        title_bar_pad_status(ui, port, link, palette->text_highlight);
        if (quick_menu_arrow(ui))
        {
            action = NavAction::menu;
        }
    }
    NavAction pad = draw_pad(ui, &frame, layout, editor, hotkeys);
    return pad != NavAction::none ? pad : action;
}
