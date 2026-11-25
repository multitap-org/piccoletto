#pragma once

// piccoletto in the browser with its controls in the page (Andrés,
// 2026-11-22: "the app remains the same, the controls are outside"). The
// app runs as everywhere else; only its ImGui screens aren't drawn (the
// canvas shows REMOTE PLAY's picture). The page drives it through this
// bridge:
//
//   page -> app   piccoletto_web_command(command, a, b, text): queued here,
//                 run by main.cpp at the start of the next frame through its
//                 own go_to / handle_action (one path for both UIs);
//                 piccoletto_web_pad(bits): the HTML touch pad, as RetroPad
//                 bits (merged with the real gamepads like the touch pad).
//   app -> page   piccoletto_web_state(): the app's state as JSON, written
//                 by main.cpp into a fixed buffer every frame.
//
// Natively the bridge is compiled but never active.

#include <cstddef>
#include <cstdint>

// What the page can ask for (the numbers are the page's too: piccoletto-page.js).
enum class WebCommand : int32_t
{
    go = 0,               // a: the screen (Screen: 0 home, 1 pair, 2 gamepad, 3 remote_play, 4 settings)
    select = 1,           // a: HOME's selected host (index in hosts.known)
    menu = 2,             // a: 1 opens the quick menu, 0 closes it
    pair_enter = 3,       // text: the code typed (PAIR A DEVICE; in a browser ENTER A CODE at once)
    pair_cancel = 4,      // PAIR's CANCEL
    pair_done = 5,        // PAIRED's DONE: HOME with the new host
    forget = 6,           // a: the known host to forget
    measure = 7,          // a: the known host to speed-test
    set_name = 8,         // text: this device's name
    set_shader = 9,       // a: the preset index (the GPU path's list)
    set_pacing = 10,      // a: 0 latency, 1 balanced, 2 smooth
    set_vibrate = 11,     // a: 1 on
    headless = 12,        // a: 1 GO HEADLESS, 0 PLAY ON the host
    pause = 13,           // the host's PAUSE (toggles)
    devices = 14,         // a: 1 asks for the host's ports (DEVICES shown), 0 not
    move_device = 15,     // a: index in the host's device list, b: the port (0-based; -1 = none)
    shader_answer = 16,   // a: 1 KEEP, 0 REVERT (the slow preset check)
    open_link = 17,       // text: a link (a guest link: straight into REMOTE PLAY)
    pair_lan = 18,        // a: PAIR with the found device `a` on the LAN, or ENTER A CODE (a = their count)
    pair_mode = 19,       // a: 0 PAIR A DEVICE, 1 GENERATE PAIRING CODE (shows a code)
};

// The page pad's own buttons past the RetroPad's 16 (piccolo-pad.js: QUICK, MENU):
// both open the quick menu here.
constexpr uint32_t WEB_PAD_QUICK_BIT = 1u << 16;
constexpr uint32_t WEB_PAD_MENU_BIT  = 1u << 17;

constexpr size_t  WEB_SHELL_TEXT       = 512;
constexpr int32_t WEB_SHELL_QUEUE      = 32;
constexpr size_t  WEB_SHELL_STATE_SIZE = 32 * 1024;

struct WebShellCommand
{
    WebCommand command = WebCommand::go;
    int32_t    a = 0;
    int32_t    b = 0;
    char       text[WEB_SHELL_TEXT] = {};
};

struct WebShell
{
    bool            active = false;     // the page has the controls
    WebShellCommand queue[WEB_SHELL_QUEUE];
    int32_t         count = 0;
    uint32_t        pad_bits = 0;       // the HTML touch pad (RetroPad bits; 16 = the quick menu)
    char            state[WEB_SHELL_STATE_SIZE] = {};
    size_t          used = 0;           // writing: bytes in `state`
    bool            overflow = false;   // writing: it didn't fit (the page keeps the last whole one)
    bool            first = true;       // writing: the next item opens its object / array (no comma)
    char            published[WEB_SHELL_STATE_SIZE] = {};   // the last whole state, what the page reads
};

// The bridge's exported functions talk to this one (main.cpp's App's).
void web_shell_attach(WebShell* shell);

// The next command the page queued, oldest first. False when none is left.
bool web_shell_next(WebShell* shell, WebShellCommand* out);

// Writing the state: begin, the pieces, end (which makes it the published
// one if it all fit). Strings are escaped for JSON; keys aren't (ours).
void web_shell_begin(WebShell* shell);
void web_shell_raw(WebShell* shell, const char* text);                  // as it is: '{', ',', "\"key\":"...
void web_shell_key(WebShell* shell, const char* key);                   // "key": (a comma before it if needed)
void web_shell_string(WebShell* shell, const char* key, const char* value);
void web_shell_int(WebShell* shell, const char* key, int64_t value);
void web_shell_bool(WebShell* shell, const char* key, bool value);
void web_shell_number(WebShell* shell, const char* key, double value);
void web_shell_open(WebShell* shell, const char* key, char bracket);    // "key":{ or "key":[ (key nullptr: in an array)
void web_shell_close(WebShell* shell, char bracket);
void web_shell_end(WebShell* shell);
