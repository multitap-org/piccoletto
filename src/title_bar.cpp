#include "title_bar.h"

#include <cmath>
#include <cstdio>

#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_MENU, _ROTATE, _ARROW_LEFT
#include "port_indicators.h"

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

// The LEDs and the ping bars: retroui's port_indicators.h (piccolo's tabs draw the same).
float title_bar_port_leds(const TouchUi* ui, float x, float y, int32_t port)
{
    const RetroUiPalette* palette = &ui->palette;
    for (int32_t i = 0; i < TITLE_BAR_PORTS; i++)
    {
        bool lit = i + 1 == port;
        touch_ui_rect(ui, x + static_cast<float>(i) * (PORT_LED_SIZE + PORT_LED_GAP), y + 2.0f, PORT_LED_SIZE, PORT_LED_SIZE,
                      lit ? PORT_LED_ON : palette->panel_dark);
    }
    return static_cast<float>(TITLE_BAR_PORTS) * (PORT_LED_SIZE + PORT_LED_GAP) - PORT_LED_GAP;
}

void title_bar_host(const TouchUi* ui, const char* host, uint32_t text_argb)
{
    float x = std::floor((touch_ui_width(ui) - touch_ui_text_width(ui, host, TouchText::normal)) * 0.5f);
    float y = touch_ui_centered_text_y(ui, title_bar_y(ui), TITLE_BAR_ICON, TouchText::normal);
    touch_ui_text(ui, x, y, host, text_argb, TouchText::normal);
}

// The text after the bars.
static void ping_text(const PadLink* link, char* out, size_t capacity)
{
    if (link->state == LinkShown::ready)
    {
        // The loss rounded; a little but not none says "<1%".
        float percent = link->loss * 100.0f;
        int written = 0;
        if (percent > 0.0f && percent < 1.0f)
        {
            written = std::snprintf(out, capacity, "%u MS <1%%", static_cast<unsigned>(link->ping_ms));
        }
        else
        {
            written = std::snprintf(out, capacity, "%u MS %u%%", static_cast<unsigned>(link->ping_ms),
                                    static_cast<unsigned>(percent + 0.5f));
        }
        // REMOTE PLAY: frames a second and the stream's rate (and losses, when there are any).
        if (link->streaming && written > 0 && static_cast<size_t>(written) < capacity)
        {
            char* rest = out + written;
            size_t room = capacity - static_cast<size_t>(written);
            int more = std::snprintf(rest, room, " · %u FPS %.1f MBPS", static_cast<unsigned>(link->fps),
                                     static_cast<double>(link->kbps) / 1000.0);
            // GO HEADLESS: the host's window is hidden, the game is here.
            if (more > 0 && static_cast<size_t>(more) < room && link->headless)
            {
                rest += more;
                room -= static_cast<size_t>(more);
                more = std::snprintf(rest, room, " · HEADLESS");
            }
            // The host's frame delay for this stream (stream_tuning), when it said.
            if (more > 0 && static_cast<size_t>(more) < room && link->delay_ms >= 0)
            {
                rest += more;
                room -= static_cast<size_t>(more);
                more = std::snprintf(rest, room, " · DELAY %d", static_cast<int>(link->delay_ms));
            }
            if (more > 0 && static_cast<size_t>(more) < room)
            {
                rest += more;
                room -= static_cast<size_t>(more);
                // Only when there are any: frames lost, audio filled in.
                if (link->lost > 0 && link->audio_gaps > 0)
                {
                    std::snprintf(rest, room, " %u LOST %u GAPS", static_cast<unsigned>(link->lost),
                                  static_cast<unsigned>(link->audio_gaps));
                }
                else if (link->lost > 0)
                {
                    std::snprintf(rest, room, " %u LOST", static_cast<unsigned>(link->lost));
                }
                else if (link->audio_gaps > 0)
                {
                    std::snprintf(rest, room, " %u GAPS", static_cast<unsigned>(link->audio_gaps));
                }
            }
        }
    }
    else
    {
        // (CONNECTING would run into SELECT on a landscape pad.)
        std::snprintf(out, capacity, "%s", link->state == LinkShown::connecting ? "…" : "OFFLINE");
    }
}

constexpr float BARS_WIDTH = static_cast<float>(PING_BARS) * PING_BAR_WIDTH +
                             static_cast<float>(PING_BARS - 1) * PING_BAR_GAP + 5.0f;   // with the gap before the text

float title_bar_ping_width(const TouchUi* ui, const PadLink* link)
{
    char text[96];
    ping_text(link, text, sizeof(text));
    return BARS_WIDTH + touch_ui_text_width(ui, text, TouchText::normal);
}

float title_bar_ping(const TouchUi* ui, float x, float y, const PadLink* link, uint32_t text_argb)
{
    const RetroUiPalette* palette = &ui->palette;
    uint32_t lit = 0;
    uint32_t color = palette->panel_dark;
    if (link->state == LinkShown::ready)
    {
        lit = ping_bars_lit(link->ping_ms);
        color = ping_color(link->ping_ms);
    }
    // Rising bars, bottoms level with the letters' (rows 2..6 of the cell).
    float bottom = y + 7.0f;
    for (uint32_t i = 0; i < PING_BARS; i++)
    {
        float height = ping_bar_height(i);
        touch_ui_rect(ui, x + static_cast<float>(i) * (PING_BAR_WIDTH + PING_BAR_GAP), bottom - height, PING_BAR_WIDTH, height,
                      i < lit ? color : palette->panel_dark);
    }
    char text[96];
    ping_text(link, text, sizeof(text));
    return BARS_WIDTH + touch_ui_text(ui, x + BARS_WIDTH, y, text, text_argb, TouchText::normal);
}

void title_bar_pad_status(const TouchUi* ui, int32_t port, const PadLink* link, uint32_t text_argb)
{
    float x = TITLE_BAR_MARGIN;
    float y = touch_ui_height(ui) - TITLE_BAR_MARGIN - touch_ui_text_height(ui, TouchText::normal);
    x += title_bar_port_leds(ui, x, y, port) + 6.0f;
    title_bar_ping(ui, x, y, link, text_argb);
}
