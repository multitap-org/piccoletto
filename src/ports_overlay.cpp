#include "ports_overlay.h"

#include "retro_ui.h"

// Taller chips than piccolo's: the board is tapped.
constexpr float CHIP_HEIGHT = 22.0f;

static bool on_port(const NetDevice* device, uint32_t port)
{
    return (device->ports & (1u << port)) != 0;
}

static uint32_t first_port(const NetDevice* device)
{
    for (uint32_t p = 0; p < PORTS_OVERLAY_PORTS; p++)
    {
        if (on_port(device, p))
        {
            return p;
        }
    }
    return PORTS_OVERLAY_PORTS;
}

static uint64_t port_summary(const std::vector<NetDevice>* devices, uint32_t port)
{
    uint64_t summary = 1469598103934665603ull;
    for (size_t i = 0; i < devices->size(); i++)
    {
        const NetDevice* device = &(*devices)[i];
        if (on_port(device, port))
        {
            summary = (summary ^ device->controller) * 1099511628211ull;
            summary = (summary ^ device->device) * 1099511628211ull;
        }
    }
    return summary;
}

static bool listed(const std::vector<NetDevice>* devices, const NetDevice* device)
{
    for (size_t i = 0; i < devices->size(); i++)
    {
        if ((*devices)[i].controller == device->controller && (*devices)[i].device == device->device)
        {
            return true;
        }
    }
    return false;
}

static PortsViewChip chip_for(const NetDevice* device)
{
    PortsViewChip chip;
    chip.key = device->controller;
    chip.label = device->name;
    chip.yours = device->owner == NetDeviceOwner::yours;
    chip.has_ping = chip.yours;   // only this phone's link is known here
    return chip;
}

// Every port's chips from the list (only when it changed). Greyed chips of
// devices that left stay until the tabs go down.
static void rebuild(PortsOverlay* overlay, const std::vector<NetDevice>* devices)
{
    for (uint32_t p = 0; p < PORTS_OVERLAY_PORTS; p++)
    {
        PortsViewColumn* column = &overlay->view.ports[p];
        std::vector<PortsViewChip> gone;
        for (size_t i = 0; i < column->chips.size(); i++)
        {
            if (column->chips[i].gone)
            {
                gone.push_back(column->chips[i]);
            }
        }
        column->chips.clear();
        for (size_t i = 0; i < devices->size(); i++)
        {
            if (on_port(&(*devices)[i], p))
            {
                column->chips.push_back(chip_for(&(*devices)[i]));
            }
        }
        for (size_t i = 0; i < gone.size(); i++)
        {
            bool back = false;   // (it came back: no grey copy)
            for (size_t c = 0; c < column->chips.size(); c++)
            {
                back = back || column->chips[c].key == gone[i].key;
            }
            if (!back)
            {
                column->chips.push_back(gone[i]);
            }
        }
    }
}

void ports_overlay_reset(PortsOverlay* overlay)
{
    ports_view_reset(&overlay->view);
    overlay->known = false;
    overlay->last.clear();
}

void ports_overlay_update(PortsOverlay* overlay, const std::vector<NetDevice>* devices, bool known, const PadLink* link,
                          uint64_t now_ms)
{
    PortsView* view = &overlay->view;
    view->show_out = false;   // the host lists only controllers on a port
    view->chip_height = CHIP_HEIGHT;
    view->port_count = PORTS_OVERLAY_PORTS;
    if (!known)
    {
        if (overlay->known)
        {
            ports_overlay_reset(overlay);
            view->port_count = PORTS_OVERLAY_PORTS;
        }
        return;
    }
    bool changed[PORTS_VIEW_MAX_PORTS] = {};
    bool any = false;
    for (uint32_t p = 0; p < PORTS_OVERLAY_PORTS; p++)
    {
        uint64_t summary = port_summary(devices, p);
        changed[p] = overlay->known && summary != overlay->summary[p];
        any = any || changed[p];
        overlay->summary[p] = summary;
    }
    if (!overlay->known || any)
    {
        if (overlay->known)
        {
            // Who left: greyed on the port they were on.
            for (size_t i = 0; i < overlay->last.size(); i++)
            {
                const NetDevice* device = &overlay->last[i];
                uint32_t port = first_port(device);
                if (port < PORTS_OVERLAY_PORTS && !listed(devices, device))
                {
                    PortsViewChip chip = chip_for(device);
                    chip.gone = true;
                    view->ports[port].chips.push_back(chip);
                }
            }
        }
        rebuild(overlay, devices);
        overlay->last = *devices;   // (only on a change)
        if (overlay->known)
        {
            ports_view_pop(view, changed, now_ms);
        }
        overlay->known = true;
    }
    // This phone's pads: this link's ping.
    for (uint32_t p = 0; p < PORTS_OVERLAY_PORTS; p++)
    {
        std::vector<PortsViewChip>* chips = &view->ports[p].chips;
        for (size_t i = 0; i < chips->size(); i++)
        {
            if ((*chips)[i].has_ping)
            {
                (*chips)[i].ping_known = link->state == LinkShown::ready;
                (*chips)[i].ping_ms = link->ping_ms;
            }
        }
    }
}

void ports_overlay_draw_tabs(PortsOverlay* overlay, const TouchUi* ui, bool hidden, uint64_t now_ms)
{
    if (!overlay->known)
    {
        return;
    }
    retro_ui_set_palette(&ui->palette);   // (ports_view draws with retroui's palette)
    PortsViewText text;
    ports_view_draw_tabs(&overlay->view, ui->font, ui->scale, &text, false, hidden, true, now_ms);
}

NavAction ports_overlay_draw_board(PortsOverlay* overlay, const TouchUi* ui, uint64_t now_ms, const std::vector<NetDevice>* devices,
                                   bool known, PortsOverlayMove* move)
{
    *move = PortsOverlayMove{};
    retro_ui_set_palette(&ui->palette);
    PortsView* view = &overlay->view;
    int32_t column = 0;
    if (view->has_pick && !ports_view_find(view, view->picked, &column))
    {
        view->has_pick = false;   // gone from the new list
    }
    PortsViewText text;
    text.help = !known ? "ASKING THE HOST\xE2\x80\xA6" : (view->has_pick ? "TAP ITS NEW PORT" : "TAP A DEVICE TO MOVE IT");
    PortsViewBoardInput input;
    ports_view_draw_board(view, ui->font, ui->scale, &text, now_ms, &input);
    if (input.move)
    {
        for (size_t i = 0; i < devices->size(); i++)
        {
            if ((*devices)[i].controller == input.key)
            {
                move->move = true;
                move->index = static_cast<int32_t>(i);
                move->port = static_cast<int8_t>(input.port);
            }
        }
        view->has_pick = false;
    }
    return input.done ? NavAction::devices : NavAction::none;
}
