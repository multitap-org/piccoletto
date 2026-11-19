#include "home_screen.h"

#include <cstdio>

#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_MENU, _ROTATE

// Layout, in retro pixels (half the mockups' dp).
constexpr float MARGIN        = 8.0f;
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float CARD_PADDING  = 10.0f;
constexpr float MAIN_BUTTON   = 36.0f;   // GAMEPAD
constexpr float SMALL_BUTTON  = 28.0f;   // REMOTE PLAY, DEVICES
constexpr float BUTTON_GAP    = 4.0f;
constexpr float ROW_HEIGHT    = 36.0f;
constexpr float PAIR_HEIGHT   = 32.0f;
constexpr float LAMP          = TOUCH_UI_LAMP;

// A light and a status word ("ONLINE"), the light level with the letters
// (rows 2..6 of the font's cell).
static void draw_status(const TouchUi* ui, float x, float y, bool online, const char* word)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_lamp(ui, x, y, online ? TOUCH_UI_LAMP_ONLINE : palette->text_disabled);
    if (word != nullptr)
    {
        touch_ui_text(ui, x + LAMP + 5.0f, y, word, palette->text_disabled, TouchText::normal);
    }
}

// The buttons a host's card has: only the modes this phone was granted.
static int32_t small_button_count(const HostAccess* access)
{
    return (access->remote_play ? 1 : 0) + (access->devices ? 1 : 0);
}

static float card_buttons_height(const HostAccess* access)
{
    float height = 0.0f;
    if (access->gamepad)
    {
        height += MAIN_BUTTON;
    }
    if (small_button_count(access) > 0)
    {
        height += (height > 0.0f ? BUTTON_GAP : 0.0f) + SMALL_BUTTON;
    }
    return height;
}

// The selected host: status, name, game, and its modes at the bottom.
static NavAction draw_card(const TouchUi* ui, const HomeHost* host, float x, float y, float width, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, height, palette->panel_face);

    float inner_x = x + CARD_PADDING;
    float inner_width = width - 2.0f * CARD_PADDING;
    float line = y + CARD_PADDING;
    draw_status(ui, inner_x, line, host->online, host->online ? "ONLINE" : "OFFLINE");
    line += touch_ui_text_height(ui, TouchText::normal) + 10.0f;
    touch_ui_text(ui, inner_x, line, host->name, palette->text_highlight, TouchText::big);
    line += touch_ui_text_height(ui, TouchText::big) + 6.0f;
    if (host->playing[0] != '\0')
    {
        touch_ui_text(ui, inner_x, line, host->playing, palette->text_disabled, TouchText::normal);
    }

    if (!host->online)
    {
        return NavAction::none;   // nothing to connect to: no buttons
    }

    NavAction action = NavAction::none;
    float button_y = y + height - CARD_PADDING - card_buttons_height(&host->access);
    if (host->access.gamepad)
    {
        if (touch_ui_button(ui, "##gamepad", inner_x, button_y, inner_width, MAIN_BUTTON, "GAMEPAD",
                            TouchButton::primary, TouchText::big))
        {
            action = NavAction::gamepad;
        }
        button_y += MAIN_BUTTON + BUTTON_GAP;
    }
    int32_t small_count = small_button_count(&host->access);
    if (small_count > 0)
    {
        float small_width = small_count == 2 ? (inner_width - BUTTON_GAP) * 0.5f : inner_width;
        float small_x = inner_x;
        if (host->access.remote_play)
        {
            if (touch_ui_button(ui, "##remote_play", small_x, button_y, small_width, SMALL_BUTTON, "REMOTE PLAY",
                                TouchButton::normal, TouchText::normal))
            {
                action = NavAction::remote_play;
            }
            small_x += small_width + BUTTON_GAP;
        }
        if (host->access.devices)
        {
            if (touch_ui_button(ui, "##devices", small_x, button_y, small_width, SMALL_BUTTON, "DEVICES",
                                TouchButton::normal, TouchText::normal))
            {
                action = NavAction::devices;
            }
        }
    }
    return action;
}

// The card's height for a host: its text and buttons, at least `minimum`.
static float card_height(const TouchUi* ui, const HomeHost* host)
{
    float text = CARD_PADDING + touch_ui_text_height(ui, TouchText::normal) + 10.0f + touch_ui_text_height(ui, TouchText::big) +
                 6.0f + touch_ui_text_height(ui, TouchText::normal);
    float buttons = host->online ? card_buttons_height(&host->access) : 0.0f;
    return text + 14.0f + buttons + CARD_PADDING;
}

// The other hosts, one row each: tapping one puts it on the card.
static float draw_rows(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count, float x, float y,
                       float width)
{
    const RetroUiPalette* palette = &ui->palette;
    for (int32_t i = 0; i < host_count; i++)
    {
        if (i == home->selected)
        {
            continue;
        }
        const HomeHost* host = &hosts[i];
        char id[24];
        std::snprintf(id, sizeof(id), "##host%d", static_cast<int>(i));
        if (touch_ui_button(ui, id, x, y, width, ROW_HEIGHT, nullptr, TouchButton::card, TouchText::normal))
        {
            home->selected = i;
        }
        float text_y = y + 14.0f;
        draw_status(ui, x + 10.0f, text_y, host->online, nullptr);
        touch_ui_text(ui, x + 10.0f + LAMP + 8.0f, text_y, host->name,
                      host->online ? palette->text_highlight : palette->text_disabled, TouchText::normal);
        const char* status = host->online ? "ONLINE" : "OFFLINE";
        float status_width = touch_ui_text_width(ui, status, TouchText::normal);
        touch_ui_text(ui, x + width - 10.0f - status_width, text_y, status, palette->text_disabled, TouchText::normal);
        y += ROW_HEIGHT + BUTTON_GAP;
    }
    return y;
}

// PAIR A DEVICE, with a badge for the unpaired ones found on the network.
static bool draw_pair_button(const TouchUi* ui, int32_t found_count, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    bool tapped = touch_ui_button(ui, "##pair", x, y, width, PAIR_HEIGHT, "+ PAIR A DEVICE", TouchButton::ghost,
                                  TouchText::normal);
    if (found_count > 0)
    {
        char badge[24];
        std::snprintf(badge, sizeof(badge), "%d FOUND", static_cast<int>(found_count));
        float badge_width = touch_ui_text_width(ui, badge, TouchText::normal) + 8.0f;
        float badge_x = x + width - 6.0f - badge_width;
        float badge_y = y + (PAIR_HEIGHT - 14.0f) * 0.5f;
        touch_ui_rect(ui, badge_x, badge_y, badge_width, 14.0f, palette->highlight);
        touch_ui_text(ui, badge_x + 4.0f, badge_y + 2.5f, badge, palette->text_highlight, TouchText::normal);
    }
    return tapped;
}

NavAction home_screen_draw(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count,
                            int32_t found_count)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    if (home->selected < 0 || home->selected >= host_count)
    {
        home->selected = 0;
    }

    // Header: the name, and the menu at the right.
    float header_y = landscape ? 6.0f : MARGIN;
    float title_y = header_y + (HEADER_HEIGHT - touch_ui_text_height(ui, TouchText::big)) * 0.5f;
    touch_ui_text(ui, MARGIN + 4.0f, title_y, "piccoletto", ui->palette.text_highlight, TouchText::big);
    if (touch_ui_button(ui, "##rotate", width - MARGIN - 2.0f * HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }
    if (touch_ui_button(ui, "##menu", width - MARGIN - HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_MENU, TouchButton::plain, TouchText::big))
    {
        action = NavAction::menu;
    }

    float content_y = header_y + HEADER_HEIGHT + (landscape ? 6.0f : 12.0f);
    if (landscape)
    {
        // The card on the left, full height; rows and PAIR on the right.
        float column = (width - 3.0f * MARGIN) * 0.5f;
        float right_x = MARGIN + column + MARGIN;
        if (host_count > 0)
        {
            NavAction card = draw_card(ui, &hosts[home->selected], MARGIN, content_y, column, height - content_y - MARGIN);
            action = card != NavAction::none ? card : action;
        }
        draw_rows(home, ui, hosts, host_count, right_x, content_y, column);
        if (draw_pair_button(ui, found_count, right_x, height - MARGIN - PAIR_HEIGHT, column))
        {
            action = NavAction::pair;
        }
        return action;
    }

    float column = width - 2.0f * MARGIN;
    float y = content_y;
    if (host_count > 0)
    {
        float card = card_height(ui, &hosts[home->selected]);
        NavAction tapped = draw_card(ui, &hosts[home->selected], MARGIN, y, column, card);
        action = tapped != NavAction::none ? tapped : action;
        y += card + MARGIN;
    }
    draw_rows(home, ui, hosts, host_count, MARGIN, y, column);
    if (draw_pair_button(ui, found_count, MARGIN, height - 12.0f - PAIR_HEIGHT, column))
    {
        action = NavAction::pair;
    }
    return action;
}
