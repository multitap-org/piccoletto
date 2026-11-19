#include "home_screen.h"

#include <cmath>
#include <cstdio>

#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_ARROW_RIGHT
#include "title_bar.h"

// Layout, in retro pixels (half the mockups' dp).
constexpr float MARGIN        = 8.0f;
constexpr float CARD_PADDING  = 10.0f;
constexpr float MAIN_BUTTON   = 36.0f;   // GAMEPAD
constexpr float SMALL_BUTTON  = 28.0f;   // REMOTE PLAY, DEVICES
constexpr float BUTTON_GAP    = 4.0f;
constexpr float ROW_HEIGHT    = 36.0f;
constexpr float LAMP          = TOUCH_UI_LAMP;
constexpr float TAB_WIDTH     = 14.0f;   // landscape: the sidebar's tab at the left edge
constexpr float SIDEBAR_MAX   = 0.8f;    // of the screen's width
constexpr float SIDEBAR_CARD  = 64.0f;   // a host's column card

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

// How many hosts the sidebar holds: all but the one on the card.
static int32_t other_count(int32_t host_count)
{
    return host_count > 1 ? host_count - 1 : 0;
}

// The tab at the left edge that opens the sidebar: an arrow and how many
// hosts are behind it.
static void draw_tab(HomeScreen* home, const TouchUi* ui, int32_t others, float y, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    bool held = false;
    if (touch_ui_area(ui, "##sidebar_tab", 0.0f, y, TAB_WIDTH, height, &held))
    {
        home->sidebar_open = true;
    }
    touch_ui_rect(ui, 0.0f, y, TAB_WIDTH, height, held ? palette->highlight : palette->panel_face);
    float middle = y + height * 0.5f;
    float arrow_width = touch_ui_text_width(ui, PIXEL_FONT_UTF8_ARROW_RIGHT, TouchText::normal);
    touch_ui_text(ui, std::floor((TAB_WIDTH - arrow_width) * 0.5f), std::floor(middle - 10.0f), PIXEL_FONT_UTF8_ARROW_RIGHT,
                  palette->text_highlight, TouchText::normal);
    char count[16];
    std::snprintf(count, sizeof(count), "%d", static_cast<int>(others));
    float count_width = touch_ui_text_width(ui, count, TouchText::normal);
    touch_ui_text(ui, std::floor((TAB_WIDTH - count_width) * 0.5f), std::floor(middle + 2.0f), count, palette->text_disabled,
                  TouchText::normal);
}

NavAction home_screen_draw(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count,
                            int32_t found_count)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    if (home->selected < 0 || home->selected >= host_count)
    {
        home->selected = 0;
    }

    // Title bar: the name (a logo later) on the left, the icons on the right.
    float header_y = title_bar_y(ui);
    float title_y = touch_ui_centered_text_y(ui, header_y, TITLE_BAR_ICON, TouchText::big);
    touch_ui_text(ui, MARGIN + 4.0f, title_y, "piccoletto", ui->palette.text_highlight, TouchText::big);
    NavAction action = title_bar_icons(ui, true, found_count);

    float content_y = title_bar_bottom(ui) + (landscape ? 6.0f : 12.0f);
    if (landscape)
    {
        // The card across the screen; the others behind the tab.
        float card_x = MARGIN;
        int32_t others = other_count(host_count);
        if (others > 0)
        {
            draw_tab(home, ui, others, content_y, height - content_y - MARGIN);
            card_x = TAB_WIDTH + MARGIN;
        }
        if (host_count > 0)
        {
            NavAction card = draw_card(ui, &hosts[home->selected], card_x, content_y, width - card_x - MARGIN,
                                       height - content_y - MARGIN);
            action = card != NavAction::none ? card : action;
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
    return action;
}

// One host in the sidebar, a column card like a port in DEVICES: its status,
// its name, what it's playing. Tapping it puts it on HOME's card.
static bool draw_sidebar_host(const TouchUi* ui, const HomeHost* host, int32_t index, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    char id[24];
    std::snprintf(id, sizeof(id), "##side_host%d", static_cast<int>(index));
    bool tapped = touch_ui_button(ui, id, x, y, width, SIDEBAR_CARD, nullptr, TouchButton::card, TouchText::normal);
    float line = y + CARD_PADDING;
    draw_status(ui, x + CARD_PADDING, line, host->online, host->online ? "ONLINE" : "OFFLINE");
    line += touch_ui_text_height(ui, TouchText::normal) + 8.0f;
    touch_ui_text(ui, x + CARD_PADDING, line, host->name, host->online ? palette->text_highlight : palette->text_disabled,
                  TouchText::normal);
    line += touch_ui_text_height(ui, TouchText::normal) + 6.0f;
    if (host->playing[0] != '\0')
    {
        touch_ui_text(ui, x + CARD_PADDING, line, host->playing, palette->text_disabled, TouchText::normal);
    }
    return tapped;
}

void home_sidebar_draw(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    int32_t others = other_count(host_count);
    if (width <= height || others == 0)
    {
        home->sidebar_open = false;
        return;
    }

    // As wide as its columns (DEVICES' port width), up to 80% of the screen.
    float column = std::floor((width - 5.0f * MARGIN) / 4.0f);
    float count = static_cast<float>(others);
    float sidebar_width = count * column + (count + 1.0f) * MARGIN;
    float widest = std::floor(width * SIDEBAR_MAX);
    if (sidebar_width > widest)
    {
        sidebar_width = widest;
        column = std::floor((sidebar_width - (count + 1.0f) * MARGIN) / count);
    }

    // The rest of the screen dimmed: a tap there closes the sidebar.
    bool held = false;
    if (touch_ui_area(ui, "##sidebar_outside", sidebar_width, 0.0f, width - sidebar_width, height, &held))
    {
        home->sidebar_open = false;
    }
    touch_ui_rect(ui, sidebar_width, 0.0f, width - sidebar_width, height, 0x99000000u);
    touch_ui_rect(ui, 0.0f, 0.0f, sidebar_width, height, palette->background);

    float header_y = title_bar_y(ui);
    touch_ui_text(ui, MARGIN + 4.0f, touch_ui_centered_text_y(ui, header_y, TITLE_BAR_ICON, TouchText::big), "HOSTS",
                  palette->text_highlight, TouchText::big);
    float y = title_bar_bottom(ui) + 6.0f;
    float x = MARGIN;
    for (int32_t i = 0; i < host_count; i++)
    {
        if (i == home->selected)
        {
            continue;
        }
        if (draw_sidebar_host(ui, &hosts[i], i, x, y, column))
        {
            home->selected = i;
            home->sidebar_open = false;
        }
        x += column + MARGIN;
    }
}
