#include "menu_sheet.h"

constexpr float SHEET_PADDING = 10.0f;
constexpr float SHEET_BUTTON  = 28.0f;
constexpr float SHEET_GAP     = 4.0f;
constexpr float SHEET_WIDTH   = 175.0f;   // fits a portrait phone with margins

constexpr uint32_t DIM = 0xA0000000u;   // over the screen behind

const char* nav_action_name(NavAction action)
{
    switch (action)
    {
        case NavAction::none:        return "NONE";
        case NavAction::home:        return "HOME";
        case NavAction::menu:        return "MENU";
        case NavAction::rotate:      return "ROTATE";
        case NavAction::pair:        return "PAIR A DEVICE";
        case NavAction::gamepad:     return "GAMEPAD";
        case NavAction::remote_play: return "REMOTE PLAY";
        case NavAction::devices:     return "DEVICES";
        case NavAction::edit_layout: return "EDIT LAYOUT";
        case NavAction::edit_done:   return "EDIT LAYOUT DONE";
        case NavAction::settings:    return "SETTINGS";
        case NavAction::touch_controls: return "TOUCH CONTROLS";
        default:                     return "?";
    }
}

NavAction menu_sheet_draw(const TouchUi* ui, Screen current, bool touch_toggle, bool touch_shown)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    NavAction picked = NavAction::none;

    // ImGui gives a tap to the first item submitted under it: the buttons go
    // first, then the sheet's and the outside's areas (drawn before them).
    touch_ui_rect(ui, 0.0f, 0.0f, width, height, DIM);

    float button_count = touch_toggle ? 5.0f : 4.0f;
    float sheet_height = 2.0f * SHEET_PADDING + button_count * SHEET_BUTTON + (button_count - 1.0f) * SHEET_GAP;
    float sheet_width = SHEET_WIDTH < width - 16.0f ? SHEET_WIDTH : width - 16.0f;
    float x = (width - sheet_width) * 0.5f;
    float y = (height - sheet_height) * 0.5f;
    touch_ui_rect(ui, x, y, sheet_width, sheet_height, ui->palette.panel_face);

    float button_x = x + SHEET_PADDING;
    float button_width = sheet_width - 2.0f * SHEET_PADDING;
    float button_y = y + SHEET_PADDING;
    bool in_remote_play = current == Screen::remote_play;
    const char* other = in_remote_play ? "GAMEPAD" : "REMOTE PLAY";
    if (touch_ui_button(ui, "##sheet_switch", button_x, button_y, button_width, SHEET_BUTTON, other, TouchButton::primary,
                        TouchText::normal))
    {
        picked = in_remote_play ? NavAction::gamepad : NavAction::remote_play;
    }
    button_y += SHEET_BUTTON + SHEET_GAP;
    if (touch_ui_button(ui, "##sheet_edit", button_x, button_y, button_width, SHEET_BUTTON, "EDIT LAYOUT", TouchButton::normal,
                        TouchText::normal))
    {
        picked = NavAction::edit_layout;
    }
    button_y += SHEET_BUTTON + SHEET_GAP;
    if (touch_toggle)
    {
        const char* label = touch_shown ? "HIDE TOUCH CONTROLS" : "SHOW TOUCH CONTROLS";
        if (touch_ui_button(ui, "##sheet_touch", button_x, button_y, button_width, SHEET_BUTTON, label, TouchButton::normal,
                            TouchText::normal))
        {
            picked = NavAction::touch_controls;
        }
        button_y += SHEET_BUTTON + SHEET_GAP;
    }
    if (touch_ui_button(ui, "##sheet_rotate", button_x, button_y, button_width, SHEET_BUTTON, "ROTATE", TouchButton::normal,
                        TouchText::normal))
    {
        picked = NavAction::rotate;
    }
    button_y += SHEET_BUTTON + SHEET_GAP;
    if (touch_ui_button(ui, "##sheet_leave", button_x, button_y, button_width, SHEET_BUTTON, "LEAVE", TouchButton::normal,
                        TouchText::normal))
    {
        picked = NavAction::home;
    }

    // Taps on the sheet between buttons do nothing; outside it, they close it.
    bool held = false;
    touch_ui_area(ui, "##sheet", x, y, sheet_width, sheet_height, &held);
    if (touch_ui_area(ui, "##sheet_outside", 0.0f, 0.0f, width, height, &held))
    {
        picked = NavAction::menu;
    }
    return picked;
}
