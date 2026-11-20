#include "devices_overlay.h"

#include <cstdio>

// Layout, in retro pixels.
constexpr float MARGIN       = 8.0f;
constexpr float HEADER       = 24.0f;
constexpr float CARD_PADDING = 6.0f;
constexpr float CARD_TITLE   = 14.0f;   // the port's name line
constexpr float CHIP_HEIGHT  = 26.0f;   // one device
constexpr float GAP          = 4.0f;
constexpr float EMPTY_HEIGHT = 14.0f;   // "NOBODY"
constexpr float DONE_WIDTH   = 52.0f;
constexpr float HINT_HEIGHT  = 14.0f;   // the line under the header
constexpr float YOURS_BAR    = 3.0f;    // marks this phone's pads

// Over the game: a light dim, and cards the game shows through (low alpha).
constexpr uint32_t DIM = 0x50000000u;
constexpr uint32_t CARD_ALPHA = 0xB0000000u;

static bool on_port(const NetDevice* device, int32_t port)
{
    return (device->ports & (1u << port)) != 0;
}

static int32_t picked_index(const DevicesOverlay* overlay, const std::vector<NetDevice>* devices)
{
    if (!overlay->picked)
    {
        return -1;
    }
    for (size_t i = 0; i < devices->size(); i++)
    {
        if ((*devices)[i].controller == overlay->picked_controller && (*devices)[i].device == overlay->picked_device)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

static int32_t count_on_port(const std::vector<NetDevice>* devices, int32_t port)
{
    int32_t count = 0;
    for (size_t i = 0; i < devices->size(); i++)
    {
        count += on_port(&(*devices)[i], port) ? 1 : 0;
    }
    return count;
}

static float card_height(const std::vector<NetDevice>* devices, int32_t port, bool move_here)
{
    int32_t count = count_on_port(devices, port);
    float content = count == 0 ? EMPTY_HEIGHT : static_cast<float>(count) * (CHIP_HEIGHT + GAP) - GAP;
    float move = move_here ? GAP + CHIP_HEIGHT : 0.0f;
    return CARD_PADDING + CARD_TITLE + content + move + CARD_PADDING;
}

// One port: its devices (the picked one in the highlight color, this
// phone's marked), MOVE HERE while a device from elsewhere is picked.
static void draw_port(DevicesOverlay* overlay, const TouchUi* ui, const std::vector<NetDevice>* devices, int32_t port,
                      int32_t picked, float x, float y, float width, float height, DevicesMove* move)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, height, (palette->panel_face & 0x00FFFFFFu) | CARD_ALPHA);
    char title[24];
    std::snprintf(title, sizeof(title), "PORT %d", static_cast<int>(port + 1));
    touch_ui_text(ui, x + CARD_PADDING + 2.0f, y + CARD_PADDING, title, palette->text_disabled, TouchText::normal);

    float chip_y = y + CARD_PADDING + CARD_TITLE;
    float chip_width = width - 2.0f * CARD_PADDING;
    int32_t shown = 0;
    for (size_t i = 0; i < devices->size(); i++)
    {
        const NetDevice* device = &(*devices)[i];
        if (!on_port(device, port))
        {
            continue;
        }
        char id[32];
        std::snprintf(id, sizeof(id), "##device%d_%d", static_cast<int>(port), static_cast<int>(i));
        TouchButton style = static_cast<int32_t>(i) == picked ? TouchButton::primary : TouchButton::normal;
        bool clicked = touch_ui_button(ui, id, x + CARD_PADDING, chip_y, chip_width, CHIP_HEIGHT, device->name.c_str(),
                                       style, TouchText::normal);
        if (device->owner == NetDeviceOwner::yours)
        {
            touch_ui_rect(ui, x + CARD_PADDING, chip_y, YOURS_BAR, CHIP_HEIGHT, palette->text_highlight);   // this phone
        }
        if (clicked)
        {
            // Tap to pick it, tap again to put it down.
            bool same = static_cast<int32_t>(i) == picked;
            overlay->picked = !same;
            overlay->picked_controller = device->controller;
            overlay->picked_device = device->device;
        }
        chip_y += CHIP_HEIGHT + GAP;
        shown++;
    }
    if (shown == 0)
    {
        touch_ui_text(ui, x + CARD_PADDING + 2.0f, chip_y + 1.0f, "NOBODY", palette->text_disabled, TouchText::normal);
        chip_y += EMPTY_HEIGHT + GAP;
    }
    if (picked >= 0 && !on_port(&(*devices)[static_cast<size_t>(picked)], port))
    {
        char id[24];
        std::snprintf(id, sizeof(id), "##move%d", static_cast<int>(port));
        if (touch_ui_button(ui, id, x + CARD_PADDING, chip_y, chip_width, CHIP_HEIGHT, "MOVE HERE", TouchButton::ghost,
                            TouchText::normal))
        {
            move->move = true;
            move->index = picked;
            move->port = static_cast<int8_t>(port);
            overlay->picked = false;
        }
    }
}

NavAction devices_overlay_draw(DevicesOverlay* overlay, const TouchUi* ui, const std::vector<NetDevice>* devices,
                               bool known, DevicesMove* move)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    *move = DevicesMove{};

    // ImGui gives a tap to the first item submitted under it: the buttons go
    // first, the outside's area (closes) last.
    touch_ui_rect(ui, 0.0f, 0.0f, width, height, DIM);

    float header_y = MARGIN;
    touch_ui_rect(ui, MARGIN, header_y, width - 2.0f * MARGIN, HEADER, (palette->panel_dark & 0x00FFFFFFu) | CARD_ALPHA);
    touch_ui_text(ui, MARGIN + 6.0f, touch_ui_centered_text_y(ui, header_y, HEADER, TouchText::normal), "DEVICES",
                  palette->text_highlight, TouchText::normal);
    int32_t picked = picked_index(overlay, devices);
    if (picked < 0)
    {
        overlay->picked = false;   // gone from the new list
    }
    const char* hint = !known ? "ASKING THE HOST…" : (picked >= 0 ? "TAP MOVE HERE ON ITS NEW PORT" : "TAP A DEVICE TO MOVE IT");
    if (touch_ui_button(ui, "##devices_done", width - MARGIN - DONE_WIDTH, header_y, DONE_WIDTH, HEADER, "DONE",
                        TouchButton::primary, TouchText::normal))
    {
        action = NavAction::devices;
    }

    // The hint on its own line (a portrait phone has no room beside the title).
    float hint_y = header_y + HEADER + GAP;
    touch_ui_rect(ui, MARGIN, hint_y, width - 2.0f * MARGIN, HINT_HEIGHT, (palette->panel_dark & 0x00FFFFFFu) | CARD_ALPHA);
    touch_ui_text(ui, MARGIN + 6.0f, touch_ui_centered_text_y(ui, hint_y, HINT_HEIGHT, TouchText::normal), hint,
                  palette->text_disabled, TouchText::normal);
    float content_y = hint_y + HINT_HEIGHT + GAP;
    if (known)
    {
        // Landscape: four columns; portrait: two by two.
        int32_t columns = landscape ? DEVICES_PORTS : 2;
        float column_width = (width - 2.0f * MARGIN - static_cast<float>(columns - 1) * GAP) / static_cast<float>(columns);
        float row_y = content_y;
        for (int32_t first = 0; first < DEVICES_PORTS; first += columns)
        {
            float row_height = 0.0f;
            for (int32_t port = first; port < first + columns && port < DEVICES_PORTS; port++)
            {
                bool move_here = picked >= 0 && !on_port(&(*devices)[static_cast<size_t>(picked)], port);
                float card = card_height(devices, port, move_here);
                row_height = card > row_height ? card : row_height;
            }
            for (int32_t port = first; port < first + columns && port < DEVICES_PORTS; port++)
            {
                float x = MARGIN + static_cast<float>(port - first) * (column_width + GAP);
                draw_port(overlay, ui, devices, port, picked, x, row_y, column_width, row_height, move);
            }
            row_y += row_height + GAP;
        }
    }

    bool held = false;
    if (touch_ui_area(ui, "##devices_outside", 0.0f, 0.0f, width, height, &held, TouchNav::taps_only) && !move->move)
    {
        if (overlay->picked)
        {
            overlay->picked = false;   // a tap beside: put the picked one down first
        }
        else
        {
            action = NavAction::devices;
        }
    }
    return action;
}
