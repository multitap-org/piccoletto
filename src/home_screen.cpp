#include "home_screen.h"

#include <cmath>
#include <cstdio>

#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_ARROW_*
#include "title_bar.h"

// Layout, in retro pixels (half the mockups' dp).
constexpr float MARGIN        = 8.0f;
constexpr float CARD_PADDING  = 10.0f;
constexpr float MAIN_BUTTON   = 36.0f;   // GAMEPAD
constexpr float SMALL_BUTTON  = 28.0f;   // REMOTE PLAY, DEVICES
constexpr float BUTTON_GAP    = 4.0f;
constexpr float LAMP          = TOUCH_UI_LAMP;
constexpr float TAB_WIDTH     = 14.0f;   // landscape: the sidebar's tab at the right edge
constexpr float BAR_HEIGHT    = 28.0f;   // portrait: the hosts bar at the bottom
constexpr float SIDEBAR_MAX   = 0.8f;    // of the screen's width (landscape) or height (portrait)
constexpr float SIDEBAR_CARD  = 64.0f;   // a host's column card
constexpr float REFRESH_SIZE  = 24.0f;   // the bandwidth's refresh by the host's name

// A light and a status word ("ONLINE"), the light level with the letters
// (rows 2..6 of the font's cell).
// The word under a host's lamp: on the LAN now, across the internet (its
// beacon: whether it's there shows when connecting), or neither.
static const char* status_word(const HomeHost* host)
{
    if (host->online)
    {
        return "ONLINE";
    }
    return host->internet ? "INTERNET" : "OFFLINE";
}

static void draw_status(const TouchUi* ui, float x, float y, bool online, const char* word)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_lamp(ui, x, y, online ? TOUCH_UI_LAMP_ONLINE : palette->text_disabled);
    if (word != nullptr)
    {
        touch_ui_text(ui, x + LAMP + 5.0f, y, word, palette->text_disabled, TouchText::normal);
    }
}

// "12.5" or "100": Mbit/s from kbit/s, a decimal under 10.
static void format_mbps(uint32_t kbps, char* out, size_t capacity)
{
    if (kbps < 10000)
    {
        std::snprintf(out, capacity, "%u.%u", static_cast<unsigned>(kbps / 1000), static_cast<unsigned>((kbps % 1000) / 100));
    }
    else
    {
        std::snprintf(out, capacity, "%u", static_cast<unsigned>(kbps / 1000));
    }
}

// The last speed test: ▼ down ▲ up, in Mbit/s.
static void draw_bandwidth(const TouchUi* ui, const HomeHost* host, float x, float y)
{
    char down[16];
    char up[16];
    format_mbps(host->down_kbps, down, sizeof(down));
    format_mbps(host->up_kbps, up, sizeof(up));
    char text[64];
    std::snprintf(text, sizeof(text), "%s %s MBPS   %s %s MBPS", PIXEL_FONT_UTF8_ARROW_DOWN, down, PIXEL_FONT_UTF8_ARROW_UP, up);
    touch_ui_text(ui, x, y, text, ui->palette.text_disabled, TouchText::normal);
}

// The buttons a host's card has: only the modes this phone was granted.
static int32_t small_button_count(const HostAccess* access)
{
    return access->remote_play ? 1 : 0;   // (DEVICES opens from the ☰ sheet inside the modes)
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
    draw_status(ui, inner_x, line, host->online, status_word(host));
    line += touch_ui_text_height(ui, TouchText::normal) + 10.0f;
    float name_width = touch_ui_text(ui, inner_x, line, host->name, palette->text_highlight, TouchText::big);
    NavAction action = NavAction::none;
    if (host->can_measure)
    {
        // A small refresh by the name: measure the bandwidth now.
        float size = REFRESH_SIZE;
        float button_y = std::floor(line + (touch_ui_text_height(ui, TouchText::big) - size) * 0.5f);
        if (host->measuring)
        {
            touch_ui_text(ui, inner_x + name_width + 6.0f, touch_ui_centered_text_y(ui, button_y, size, TouchText::normal),
                          PIXEL_FONT_UTF8_ROTATE, palette->text_disabled, TouchText::normal);
        }
        else if (touch_ui_button(ui, "##measure", inner_x + name_width, button_y, size, size, PIXEL_FONT_UTF8_ROTATE,
                                 TouchButton::plain, TouchText::normal))
        {
            action = NavAction::measure;
        }
    }
    line += touch_ui_text_height(ui, TouchText::big) + 6.0f;
    if (host->playing[0] != '\0')
    {
        touch_ui_text(ui, inner_x, line, host->playing, palette->text_disabled, TouchText::normal);
        line += touch_ui_text_height(ui, TouchText::normal) + 6.0f;
    }
    if (host->measuring)
    {
        touch_ui_text(ui, inner_x, line, "MEASURING…", palette->text_disabled, TouchText::normal);
    }
    else if (host->down_kbps > 0 || host->up_kbps > 0)
    {
        draw_bandwidth(ui, host, inner_x, line);
    }

    if (!host->online && !host->internet)
    {
        return action;   // nothing to connect to: no mode buttons
    }

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
        if (touch_ui_button(ui, "##remote_play", inner_x, button_y, inner_width, SMALL_BUTTON, "REMOTE PLAY",
                            TouchButton::normal, TouchText::normal))
        {
            action = NavAction::remote_play;
        }
    }
    return action;
}

// The card's height for a host: its text and buttons, at least `minimum`.
static float card_height(const TouchUi* ui, const HomeHost* host)
{
    float text = CARD_PADDING + touch_ui_text_height(ui, TouchText::normal) + 10.0f + touch_ui_text_height(ui, TouchText::big) +
                 6.0f + touch_ui_text_height(ui, TouchText::normal);
    // The game and the bandwidth share that line's room; both: one more line.
    if (host->playing[0] != '\0' && (host->down_kbps > 0 || host->up_kbps > 0 || host->measuring))
    {
        text += touch_ui_text_height(ui, TouchText::normal) + 6.0f;
    }
    float buttons = (host->online || host->internet) ? card_buttons_height(&host->access) : 0.0f;
    return text + 14.0f + buttons + CARD_PADDING;
}

// How many hosts the sidebar holds: all but the one on the card.
static int32_t other_count(int32_t host_count)
{
    return host_count > 1 ? host_count - 1 : 0;
}

// Landscape: the tab at the right edge that opens the sidebar, an arrow and
// how many hosts are behind it.
static void draw_tab(HomeScreen* home, const TouchUi* ui, int32_t others, float x, float y, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    bool held = false;
    if (touch_ui_area(ui, "##sidebar_tab", x, y, TAB_WIDTH, height, &held, TouchNav::reachable))
    {
        home->sidebar_open = true;
    }
    touch_ui_rect(ui, x, y, TAB_WIDTH, height, held ? palette->highlight : palette->panel_face);
    float middle = y + height * 0.5f;
    float arrow_width = touch_ui_text_width(ui, PIXEL_FONT_UTF8_ARROW_LEFT, TouchText::normal);
    touch_ui_text(ui, std::floor(x + (TAB_WIDTH - arrow_width) * 0.5f), std::floor(middle - 10.0f), PIXEL_FONT_UTF8_ARROW_LEFT,
                  palette->text_highlight, TouchText::normal);
    char count[16];
    std::snprintf(count, sizeof(count), "%d", static_cast<int>(others));
    float count_width = touch_ui_text_width(ui, count, TouchText::normal);
    touch_ui_text(ui, std::floor(x + (TAB_WIDTH - count_width) * 0.5f), std::floor(middle + 2.0f), count, palette->text_disabled,
                  TouchText::normal);
}

// Portrait: the hosts bar across the bottom, like a status bar: an arrow,
// how many other hosts there are, and a light for each. Tapping it opens the
// sidebar upward.
static void draw_bar(HomeScreen* home, const TouchUi* ui, const HomeHost* hosts, int32_t host_count, int32_t others, float y,
                     float width)
{
    const RetroUiPalette* palette = &ui->palette;
    bool held = false;
    if (touch_ui_area(ui, "##hosts_bar", 0.0f, y, width, BAR_HEIGHT, &held, TouchNav::reachable))
    {
        home->sidebar_open = true;
    }
    touch_ui_rect(ui, 0.0f, y, width, BAR_HEIGHT, held ? palette->highlight : palette->panel_face);
    float text_y = touch_ui_centered_text_y(ui, y, BAR_HEIGHT, TouchText::normal);
    float x = MARGIN + 2.0f;
    x += touch_ui_text(ui, x, text_y, PIXEL_FONT_UTF8_ARROW_UP, palette->text_highlight, TouchText::normal) + 6.0f;
    char label[32];
    std::snprintf(label, sizeof(label), others == 1 ? "%d MORE HOST" : "%d MORE HOSTS", static_cast<int>(others));
    touch_ui_text(ui, x, text_y, label, palette->text_highlight, TouchText::normal);
    float lamp_x = width - MARGIN - 2.0f - LAMP;
    for (int32_t i = host_count - 1; i >= 0; i--)
    {
        if (i == home->selected)
        {
            continue;
        }
        touch_ui_lamp(ui, lamp_x, text_y, hosts[i].online ? TOUCH_UI_LAMP_ONLINE : palette->text_disabled);
        lamp_x -= LAMP + 4.0f;
    }
}

// No paired host yet: one card that says so, PAIR A DEVICE as its button.
static NavAction draw_empty(const TouchUi* ui, int32_t found_count, float x, float y, float width, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, height, palette->panel_face);
    float inner_x = x + CARD_PADDING;
    float line = y + CARD_PADDING;
    touch_ui_text(ui, inner_x, line, "NO DEVICES YET", palette->text_highlight, TouchText::big);
    line += touch_ui_text_height(ui, TouchText::big) + 8.0f;
    touch_ui_text(ui, inner_x, line, "PAIR ONE TO PLAY ON IT", palette->text_disabled, TouchText::normal);
    line += touch_ui_text_height(ui, TouchText::normal) + 6.0f;
    if (found_count > 0)
    {
        char found[48];
        std::snprintf(found, sizeof(found), "%d FOUND ON THIS NETWORK", static_cast<int>(found_count));
        touch_ui_text(ui, inner_x, line, found, palette->text_disabled, TouchText::normal);
    }
    float button_y = y + height - CARD_PADDING - MAIN_BUTTON;
    if (touch_ui_button(ui, "##empty_pair", inner_x, button_y, width - 2.0f * CARD_PADDING, MAIN_BUTTON, "PAIR A DEVICE",
                        TouchButton::primary, TouchText::big))
    {
        return NavAction::pair;
    }
    return NavAction::none;
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
    if (host_count == 0)
    {
        float empty_height = landscape ? height - content_y - MARGIN : 120.0f;
        NavAction empty = draw_empty(ui, found_count, MARGIN, content_y, width - 2.0f * MARGIN, empty_height);
        return empty != NavAction::none ? empty : action;
    }
    int32_t others = other_count(host_count);
    if (landscape)
    {
        // The card across the screen; the others behind the tab.
        float card_right = width - MARGIN;
        if (others > 0)
        {
            draw_tab(home, ui, others, width - TAB_WIDTH, content_y, height - content_y - MARGIN);
            card_right = width - TAB_WIDTH - MARGIN;
        }
        if (host_count > 0)
        {
            NavAction card = draw_card(ui, &hosts[home->selected], MARGIN, content_y, card_right - MARGIN,
                                       height - content_y - MARGIN);
            action = card != NavAction::none ? card : action;
        }
        return action;
    }

    // Portrait: the card under the title; the others behind the bar at the bottom.
    if (host_count > 0)
    {
        float card = card_height(ui, &hosts[home->selected]);
        NavAction tapped = draw_card(ui, &hosts[home->selected], MARGIN, content_y, width - 2.0f * MARGIN, card);
        action = tapped != NavAction::none ? tapped : action;
    }
    if (others > 0)
    {
        draw_bar(home, ui, hosts, host_count, others, height - BAR_HEIGHT, width);
    }
    return action;
}

// One host in the sidebar, a card like a port in DEVICES: its status, its
// name, what it's playing. Tapping it puts it on HOME's card.
static bool draw_sidebar_host(const TouchUi* ui, const HomeHost* host, int32_t index, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    char id[24];
    std::snprintf(id, sizeof(id), "##side_host%d", static_cast<int>(index));
    bool tapped = touch_ui_button(ui, id, x, y, width, SIDEBAR_CARD, nullptr, TouchButton::card, TouchText::normal);
    float line = y + CARD_PADDING;
    draw_status(ui, x + CARD_PADDING, line, host->online, status_word(host));
    line += touch_ui_text_height(ui, TouchText::normal) + 8.0f;
    bool reachable = host->online || host->internet;
    touch_ui_text(ui, x + CARD_PADDING, line, host->name, reachable ? palette->text_highlight : palette->text_disabled,
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
    bool landscape = width > height;
    int32_t others = other_count(host_count);
    if (others == 0)
    {
        home->sidebar_open = false;
        return;
    }
    float count = static_cast<float>(others);

    // Where the sidebar is: landscape, at the right, one column per host (as
    // wide as DEVICES' ports); portrait, at the bottom, one card per row. As
    // big as the hosts need, up to 80% of the screen.
    float side_x = 0.0f;
    float side_y = 0.0f;
    float side_width = width;
    float side_height = height;
    float card_width = width - 2.0f * MARGIN;
    float title_height = TITLE_BAR_ICON + 6.0f;
    if (landscape)
    {
        card_width = std::floor((width - 5.0f * MARGIN) / 4.0f);
        side_width = count * card_width + (count + 1.0f) * MARGIN;
        float widest = std::floor(width * SIDEBAR_MAX);
        if (side_width > widest)
        {
            side_width = widest;
            card_width = std::floor((side_width - (count + 1.0f) * MARGIN) / count);
        }
        side_x = width - side_width;
    }
    else
    {
        side_height = title_height + count * (SIDEBAR_CARD + MARGIN) + MARGIN;
        float tallest = std::floor(height * SIDEBAR_MAX);
        side_height = side_height > tallest ? tallest : side_height;
        side_y = height - side_height;
    }

    // The rest of the screen dimmed: a tap there closes the sidebar.
    float rest_x = 0.0f;
    float rest_width = landscape ? side_x : width;
    float rest_height = landscape ? height : side_y;
    bool held = false;
    if (touch_ui_area(ui, "##sidebar_outside", rest_x, 0.0f, rest_width, rest_height, &held, TouchNav::taps_only))
    {
        home->sidebar_open = false;
    }
    touch_ui_rect(ui, rest_x, 0.0f, rest_width, rest_height, 0x99000000u);
    touch_ui_rect(ui, side_x, side_y, side_width, side_height, palette->background);

    // Its title where the title bar's is in landscape; at its top in portrait.
    float header_y = landscape ? title_bar_y(ui) : side_y + 6.0f;
    touch_ui_text(ui, side_x + MARGIN + 4.0f, touch_ui_centered_text_y(ui, header_y, TITLE_BAR_ICON, TouchText::big), "HOSTS",
                  palette->text_highlight, TouchText::big);
    float x = side_x + MARGIN;
    float y = landscape ? title_bar_bottom(ui) + 6.0f : side_y + title_height;
    touch_ui_push_clip(ui, side_x, side_y, side_width, side_height);
    for (int32_t i = 0; i < host_count; i++)
    {
        if (i == home->selected)
        {
            continue;
        }
        if (draw_sidebar_host(ui, &hosts[i], i, x, y, card_width))
        {
            home->selected = i;
            home->sidebar_open = false;
        }
        if (landscape)
        {
            x += card_width + MARGIN;
        }
        else
        {
            y += SIDEBAR_CARD + MARGIN;
        }
    }
    touch_ui_pop_clip();
}
