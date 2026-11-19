#include "title_bar.h"

#include <cmath>
#include <cstdio>

#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_MENU, _ROTATE, _ARROW_LEFT

constexpr int32_t ICON_COUNT = 3;

float title_bar_y(const TouchUi* ui)
{
    bool landscape = touch_ui_width(ui) > touch_ui_height(ui);
    return landscape ? 6.0f : TITLE_BAR_MARGIN;
}

float title_bar_bottom(const TouchUi* ui)
{
    return title_bar_y(ui) + TITLE_BAR_ICON;
}

float title_bar_icons_x(const TouchUi* ui)
{
    return touch_ui_width(ui) - TITLE_BAR_MARGIN - static_cast<float>(ICON_COUNT) * TITLE_BAR_ICON;
}

// The number of unpaired devices found, small, at the +'s top right.
static void draw_found_badge(const TouchUi* ui, float x, float y, int32_t found_count)
{
    const RetroUiPalette* palette = &ui->palette;
    char count[16];
    std::snprintf(count, sizeof(count), "%d", static_cast<int>(found_count));
    float text_width = touch_ui_text_width(ui, count, TouchText::normal);
    float badge_width = text_width + 4.0f;
    float badge_height = 9.0f;
    float badge_x = std::floor(x + TITLE_BAR_ICON - badge_width);
    touch_ui_rect(ui, badge_x, y, badge_width, badge_height, palette->highlight);
    touch_ui_text(ui, badge_x + 2.0f, touch_ui_centered_text_y(ui, y, badge_height, TouchText::normal), count,
                  palette->text_highlight, TouchText::normal);
}

NavAction title_bar_icons(const TouchUi* ui, bool pair_enabled, int32_t found_count)
{
    const RetroUiPalette* palette = &ui->palette;
    NavAction action = NavAction::none;
    float x = title_bar_icons_x(ui);
    float y = title_bar_y(ui);

    if (pair_enabled)
    {
        if (touch_ui_button(ui, "##title_pair", x, y, TITLE_BAR_ICON, TITLE_BAR_ICON, "+", TouchButton::plain, TouchText::big))
        {
            action = NavAction::pair;
        }
        if (found_count > 0)
        {
            draw_found_badge(ui, x, y, found_count);
        }
    }
    else
    {
        // Off: the same glyph in the disabled color, nothing to tap.
        float text_width = touch_ui_text_width(ui, "+", TouchText::big);
        touch_ui_text(ui, std::floor(x + (TITLE_BAR_ICON - text_width) * 0.5f),
                      touch_ui_centered_text_y(ui, y, TITLE_BAR_ICON, TouchText::big), "+", palette->text_disabled, TouchText::big);
    }
    x += TITLE_BAR_ICON;
    if (touch_ui_button(ui, "##title_rotate", x, y, TITLE_BAR_ICON, TITLE_BAR_ICON, PIXEL_FONT_UTF8_ROTATE, TouchButton::plain,
                        TouchText::big))
    {
        action = NavAction::rotate;
    }
    x += TITLE_BAR_ICON;
    if (touch_ui_button(ui, "##title_menu", x, y, TITLE_BAR_ICON, TITLE_BAR_ICON, PIXEL_FONT_UTF8_MENU, TouchButton::plain,
                        TouchText::big))
    {
        action = NavAction::menu;
    }
    return action;
}

bool title_bar_back(const TouchUi* ui)
{
    return touch_ui_button(ui, "##title_back", TITLE_BAR_MARGIN - 2.0f, title_bar_y(ui), TITLE_BAR_ICON, TITLE_BAR_ICON,
                           PIXEL_FONT_UTF8_ARROW_LEFT, TouchButton::plain, TouchText::big);
}

constexpr float LED_SIZE = 5.0f;   // a letter's height, like TOUCH_UI_LAMP
constexpr float LED_GAP  = 3.0f;

float title_bar_port_leds(const TouchUi* ui, float x, float y, int32_t port)
{
    const RetroUiPalette* palette = &ui->palette;
    for (int32_t i = 0; i < TITLE_BAR_PORTS; i++)
    {
        bool lit = i + 1 == port;
        touch_ui_rect(ui, x + static_cast<float>(i) * (LED_SIZE + LED_GAP), y + 2.0f, LED_SIZE, LED_SIZE,
                      lit ? TOUCH_UI_LAMP_ONLINE : palette->panel_dark);
    }
    return static_cast<float>(TITLE_BAR_PORTS) * (LED_SIZE + LED_GAP) - LED_GAP;
}

void title_bar_status(const TouchUi* ui, const char* host, int32_t port, const char* latency, uint32_t text_argb)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%s · %s", host, latency);
    float leds_width = static_cast<float>(TITLE_BAR_PORTS) * (LED_SIZE + LED_GAP) - LED_GAP;
    float gap = 6.0f;
    float total = leds_width + gap + touch_ui_text_width(ui, text, TouchText::normal);
    float x = std::floor((touch_ui_width(ui) - total) * 0.5f);
    float y = touch_ui_centered_text_y(ui, title_bar_y(ui), TITLE_BAR_ICON, TouchText::normal);
    x += title_bar_port_leds(ui, x, y, port) + gap;
    touch_ui_text(ui, x, y, text, text_argb, TouchText::normal);
}
