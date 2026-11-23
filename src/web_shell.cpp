#include "web_shell.h"

#include <cstdio>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

static WebShell* g_shell = nullptr;   // the exported functions' (one app per page)

void web_shell_attach(WebShell* shell)
{
    g_shell = shell;
}

bool web_shell_next(WebShell* shell, WebShellCommand* out)
{
    if (shell->count == 0)
    {
        return false;
    }
    *out = shell->queue[0];
    for (int32_t i = 1; i < shell->count; i++)
    {
        shell->queue[i - 1] = shell->queue[i];
    }
    shell->count--;
    return true;
}

// --- the state as JSON ------------------------------------------------------

static void put(WebShell* shell, const char* text, size_t size)
{
    if (shell->overflow || shell->used + size + 1 > sizeof(shell->state))
    {
        shell->overflow = true;
        return;
    }
    std::memcpy(shell->state + shell->used, text, size);
    shell->used += size;
    shell->state[shell->used] = '\0';
}

static void put_text(WebShell* shell, const char* text)
{
    put(shell, text, std::strlen(text));
}

// A JSON string: quotes, backslashes and control characters escaped (names
// are typed by people: piccolo's, the phone's).
static void put_quoted(WebShell* shell, const char* text)
{
    put(shell, "\"", 1);
    for (const char* c = text; *c != '\0'; c++)
    {
        unsigned char byte = static_cast<unsigned char>(*c);
        if (byte == '"' || byte == '\\')
        {
            char escaped[2] = {'\\', static_cast<char>(byte)};
            put(shell, escaped, 2);
        }
        else if (byte < 0x20)
        {
            char escaped[8];
            int size = std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(byte));
            put(shell, escaped, static_cast<size_t>(size));
        }
        else
        {
            put(shell, c, 1);
        }
    }
    put(shell, "\"", 1);
}

void web_shell_begin(WebShell* shell)
{
    shell->used = 0;
    shell->overflow = false;
    shell->state[0] = '\0';
    shell->first = true;
    put_text(shell, "{");
}

void web_shell_raw(WebShell* shell, const char* text)
{
    put_text(shell, text);
}

void web_shell_key(WebShell* shell, const char* key)
{
    if (!shell->first)
    {
        put_text(shell, ",");
    }
    shell->first = false;
    if (key != nullptr)
    {
        put_quoted(shell, key);
        put_text(shell, ":");
    }
}

void web_shell_string(WebShell* shell, const char* key, const char* value)
{
    web_shell_key(shell, key);
    put_quoted(shell, value != nullptr ? value : "");
}

void web_shell_int(WebShell* shell, const char* key, int64_t value)
{
    web_shell_key(shell, key);
    char text[32];
    int size = std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
    put(shell, text, static_cast<size_t>(size));
}

void web_shell_bool(WebShell* shell, const char* key, bool value)
{
    web_shell_key(shell, key);
    put_text(shell, value ? "true" : "false");
}

void web_shell_number(WebShell* shell, const char* key, double value)
{
    web_shell_key(shell, key);
    char text[48];
    int size = std::snprintf(text, sizeof(text), "%.3f", value);
    put(shell, text, static_cast<size_t>(size));
}

void web_shell_open(WebShell* shell, const char* key, char bracket)
{
    web_shell_key(shell, key);
    char text[2] = {bracket, '\0'};
    put_text(shell, text);
    shell->first = true;
}

void web_shell_close(WebShell* shell, char bracket)
{
    char text[2] = {bracket, '\0'};
    put_text(shell, text);
    shell->first = false;
}

void web_shell_end(WebShell* shell)
{
    put_text(shell, "}");
    if (!shell->overflow)
    {
        std::memcpy(shell->published, shell->state, shell->used + 1);
    }
}

// --- what the page calls ----------------------------------------------------

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE int32_t piccoletto_web_command(int32_t command, int32_t a, int32_t b, const char* text)
{
    if (g_shell == nullptr || !g_shell->active || g_shell->count >= WEB_SHELL_QUEUE)
    {
        return 0;
    }
    WebShellCommand* item = &g_shell->queue[g_shell->count];
    *item = WebShellCommand{};
    item->command = static_cast<WebCommand>(command);
    item->a = a;
    item->b = b;
    std::snprintf(item->text, sizeof(item->text), "%s", text != nullptr ? text : "");
    g_shell->count++;
    return 1;
}

extern "C" EMSCRIPTEN_KEEPALIVE void piccoletto_web_pad(int32_t bits)
{
    if (g_shell != nullptr)
    {
        g_shell->pad_bits = static_cast<uint32_t>(bits);
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* piccoletto_web_state()
{
    return g_shell != nullptr ? g_shell->published : "{}";
}

// 1: the page has the controls; 0: it doesn't (?ui=app); -1: main() hasn't
// started yet (ask again).
extern "C" EMSCRIPTEN_KEEPALIVE int32_t piccoletto_web_active()
{
    if (g_shell == nullptr)
    {
        return -1;
    }
    return g_shell->active ? 1 : 0;
}
#endif
