/*
 * mod-hw-emulator — headless runner
 *
 * Loads a firmware module, runs a script of input events and writes display snapshots, printing what the
 * firmware sends on its serial channels.
 *
 *   mhe-run [--state DIR] [--tcp CHANNEL=PORT]... MODULE [COMMAND...]
 *
 * --tcp exposes a serial channel on 127.0.0.1:PORT, e.g. --tcp webgui=9900 for mod-ui
 * (MOD_HMI_SERIAL_PORT=tcp://127.0.0.1:9900). The system and cli channels are answered by the emulated
 * mod-system-control and Linux console unless they are exposed.
 *
 * Commands (as arguments, or one per line on stdin when none is given):
 *   wait MS                      let the firmware run
 *   turn INPUT STEPS             turn an encoder, negative steps anticlockwise
 *   press INPUT / release INPUT  hold or let go a button, footswitch or encoder push
 *   click INPUT                  press, 100 ms, release
 *   send CHANNEL TEXT            send TEXT plus the protocol terminator (NUL) on a serial channel
 *   snap FILE.png                write the display (4x, as it looks with its backlight)
 *   leds                         print the led colours
 *   quit
 * INPUT is an input name without spaces, case insensitive: knob1, footswitcha, button2…
 */

#include "mhe/emu_module.h"
#include "../common/module_loader.h"
#include "../common/png.h"
#include "../common/syscontrol.h"
#include "../common/console.h"
#include "../common/tcp_serial.h"

#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir_p(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_p(p) mkdir(p, 0755)
#endif

static const mhe_module *g_mod;
static mhe_syscontrol *g_sys;
static int g_system_channel = -1;
static mhe_console *g_console;
static int g_cli_channel = -1;

/* channels exposed on tcp, by channel id */
#define MAX_CHANNELS 8
static mhe_tcp_serial *g_tcp[MAX_CHANNELS];
static uint8_t g_tcp_ids[MAX_CHANNELS];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint8_t g_frame[64 * 1024];
static uint16_t g_frame_w, g_frame_h;
static uint8_t g_brightness;
static unsigned g_frames;

static uint8_t g_leds[64][3];

#define LINE_MAX_LEN 4096
static char g_lines[8][LINE_MAX_LEN];
static size_t g_line_len[8];

static void sleep_ms(unsigned ms)
{
    struct timespec ts = { .tv_sec = ms / 1000u, .tv_nsec = (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ---- host callbacks ---- */

static void host_log(void *h, mhe_log_level level, const char *message)
{
    (void)h;
    static const char *names[] = { "error", "warning", "info", "debug", "trace" };
    fprintf(stderr, "[fw:%s] %s\n", names[level <= MHE_LOG_TRACE ? level : 0], message);
}

static void host_display(void *h, uint8_t display, uint16_t w, uint16_t hgt, mhe_pixel_format format,
                         const uint8_t *bits, uint8_t brightness)
{
    (void)h; (void)format;
    if (display != 0)
        return;
    const size_t size = (size_t)hgt * ((w + 7u) / 8u);
    if (size > sizeof(g_frame))
        return;
    pthread_mutex_lock(&g_mutex);
    memcpy(g_frame, bits, size);
    g_frame_w = w;
    g_frame_h = hgt;
    g_brightness = brightness;
    g_frames++;
    pthread_mutex_unlock(&g_mutex);
}

static void host_led(void *h, uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    (void)h;
    if (led < 64)
    {
        g_leds[led][0] = r;
        g_leds[led][1] = g;
        g_leds[led][2] = b;
    }
}

static const char *channel_name(uint8_t channel)
{
    for (size_t i = 0; i < g_mod->n_serials; ++i)
        if (g_mod->serials[i].id == channel)
            return g_mod->serials[i].name;
    return "?";
}

/* the emulated mod-system-control answers on the system line */
static void sys_reply(void *ctx, const uint8_t *data, size_t size)
{
    (void)ctx;
    printf("[system<] %.*s\n", (int)size, (const char *)data);
    g_mod->serial_rx((uint8_t)g_system_channel, data, size);
}

/* the emulated Linux console answers on the cli line */
static void console_reply(void *ctx, const uint8_t *data, size_t size)
{
    (void)ctx;
    g_mod->serial_rx((uint8_t)g_cli_channel, data, size);
}

/* prints whole protocol messages (NUL terminated) or lines */
static void host_serial(void *h, uint8_t channel, const uint8_t *data, size_t size)
{
    (void)h;
    if (channel >= 8)
        return;
    if (g_tcp[channel])
        mhe_tcp_serial_send(g_tcp[channel], data, size);
    else if ((int)channel == g_system_channel && g_sys)
        mhe_syscontrol_feed(g_sys, data, size);
    else if ((int)channel == g_cli_channel && g_console)
        mhe_console_feed(g_console, data, size);
    pthread_mutex_lock(&g_mutex);
    for (size_t i = 0; i < size; ++i)
    {
        const uint8_t c = data[i];
        if (c == 0 || c == '\n')
        {
            g_lines[channel][g_line_len[channel]] = 0;
            if (g_line_len[channel])
                printf("[%s] %s\n", channel_name(channel), g_lines[channel]);
            g_line_len[channel] = 0;
        }
        else if (g_line_len[channel] < LINE_MAX_LEN - 1 && c != '\r')
        {
            g_lines[channel][g_line_len[channel]++] = isprint(c) ? (char)c : '.';
        }
    }
    fflush(stdout);
    pthread_mutex_unlock(&g_mutex);
}

/* ---- commands ---- */

static int find_input(const char *name, mhe_input_kind *kind, uint8_t *id)
{
    for (size_t i = 0; i < g_mod->n_inputs; ++i)
    {
        char compact[64];
        size_t n = 0;
        for (const char *p = g_mod->inputs[i].name; *p && n < sizeof(compact) - 1; ++p)
            if (! isspace((unsigned char)*p))
                compact[n++] = (char)tolower((unsigned char)*p);
        compact[n] = 0;

        char wanted[64];
        n = 0;
        for (const char *p = name; *p && n < sizeof(wanted) - 1; ++p)
            wanted[n++] = (char)tolower((unsigned char)*p);
        wanted[n] = 0;

        if (strcmp(compact, wanted) == 0)
        {
            *kind = g_mod->inputs[i].kind;
            *id = g_mod->inputs[i].id;
            return 1;
        }
    }
    fprintf(stderr, "unknown input '%s'\n", name);
    return 0;
}

static int find_channel(const char *name, uint8_t *channel)
{
    for (size_t i = 0; i < g_mod->n_serials; ++i)
        if (strcmp(g_mod->serials[i].name, name) == 0)
        {
            *channel = g_mod->serials[i].id;
            return 1;
        }
    fprintf(stderr, "unknown channel '%s'\n", name);
    return 0;
}

static int snap(const char *path)
{
    enum { SCALE = 4, BORDER = 8 };

    pthread_mutex_lock(&g_mutex);
    const unsigned w = g_frame_w, h = g_frame_h;
    if (! w || ! h)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "no frame yet\n");
        return -1;
    }

    const unsigned iw = w * SCALE + BORDER * 2, ih = h * SCALE + BORDER * 2;
    uint8_t *rgb = malloc((size_t)iw * ih * 3u);
    const float k = 0.25f + 0.75f * (float)g_brightness / 255.0f;
    const uint8_t lit[3] = { (uint8_t)(235 * k), (uint8_t)(240 * k), (uint8_t)(245 * k) };
    const uint8_t off[3] = { 14, 15, 18 };
    const uint8_t grid[3] = { 8, 9, 11 };

    for (unsigned y = 0; y < ih; ++y)
        for (unsigned x = 0; x < iw; ++x)
        {
            const uint8_t *c = off;
            if (x >= BORDER && y >= BORDER && x < iw - BORDER && y < ih - BORDER)
            {
                const unsigned px = (x - BORDER) / SCALE, py = (y - BORDER) / SCALE;
                const int on = (g_frame[py * ((w + 7u) / 8u) + px / 8u] >> (7u - px % 8u)) & 1;
                const int edge = ((x - BORDER) % SCALE == SCALE - 1) || ((y - BORDER) % SCALE == SCALE - 1);
                c = on ? lit : (edge ? grid : off);
            }
            memcpy(&rgb[((size_t)y * iw + x) * 3u], c, 3);
        }
    pthread_mutex_unlock(&g_mutex);

    const int r = png_write_rgb(path, rgb, iw, ih);
    free(rgb);
    if (r == 0)
        printf("snapshot %s (%u frames so far)\n", path, g_frames);
    else
        fprintf(stderr, "cannot write %s\n", path);
    return r;
}

static int run_command(char *line)
{
    char *argv[4] = { 0 };
    int argc = 0;
    char *p = line;
    while (*p && argc < 2)
    {
        while (isspace((unsigned char)*p)) ++p;
        if (! *p) break;
        argv[argc++] = p;
        while (*p && ! isspace((unsigned char)*p)) ++p;
        if (*p) *p++ = 0;
    }
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p) argv[argc++] = p; /* rest of the line */

    if (argc == 0 || argv[0][0] == '#')
        return 0;

    const char *cmd = argv[0];
    mhe_input_kind kind;
    uint8_t id;

    if (! strcmp(cmd, "quit"))
        return 1;
    if (! strcmp(cmd, "break")) /* stops in a debugger: inspect the firmware tasks */
    {
        raise(SIGTRAP);
        return 0;
    }
    if (! strcmp(cmd, "wait") && argc >= 2)
        sleep_ms((unsigned)atoi(argv[1]));
    else if (! strcmp(cmd, "turn") && argc >= 3 && find_input(argv[1], &kind, &id))
        g_mod->encoder_turn(id, atoi(argv[2]));
    else if (! strcmp(cmd, "press") && argc >= 2 && find_input(argv[1], &kind, &id))
        g_mod->input_press(kind, id, 1);
    else if (! strcmp(cmd, "release") && argc >= 2 && find_input(argv[1], &kind, &id))
        g_mod->input_press(kind, id, 0);
    else if (! strcmp(cmd, "click") && argc >= 2 && find_input(argv[1], &kind, &id))
    {
        g_mod->input_press(kind, id, 1);
        sleep_ms(100);
        g_mod->input_press(kind, id, 0);
    }
    else if (! strcmp(cmd, "send") && argc >= 3)
    {
        uint8_t channel;
        if (find_channel(argv[1], &channel))
            g_mod->serial_rx(channel, (const uint8_t *)argv[2], strlen(argv[2]) + 1u);
    }
    else if (! strcmp(cmd, "snap") && argc >= 2)
        snap(argv[1]);
    else if (! strcmp(cmd, "leds"))
    {
        for (size_t i = 0; i < g_mod->n_leds; ++i)
        {
            const uint8_t *c = g_leds[g_mod->leds[i].id];
            printf("led %-14s #%02x%02x%02x\n", g_mod->leds[i].name, c[0], c[1], c[2]);
        }
    }
    else
        fprintf(stderr, "bad command: %s\n", cmd);

    return 0;
}

/* bytes a tcp client sent: to the controller line it is bound to */
static void tcp_rx(void *ctx, const uint8_t *data, size_t size)
{
    const uint8_t channel = *(const uint8_t *)ctx;

    /* print what the client sends, message by message */
    static char line[LINE_MAX_LEN];
    static size_t len;
    for (size_t i = 0; i < size; ++i)
    {
        if (data[i] == 0 || len == LINE_MAX_LEN - 1)
        {
            line[len] = 0;
            printf("[%s<] %s\n", channel_name(channel), line);
            len = 0;
        }
        else
            line[len++] = isprint(data[i]) ? (char)data[i] : '.';
    }

    g_mod->serial_rx(channel, data, size);
}

int main(int argc, char **argv)
{
    const char *state_dir = "mhe-state";
    const char *tcp_specs[MAX_CHANNELS];
    int n_tcp = 0;
    int arg = 1;

    for (;;)
    {
        if (arg + 1 < argc && ! strcmp(argv[arg], "--state"))
            state_dir = argv[arg + 1];
        else if (arg + 1 < argc && ! strcmp(argv[arg], "--tcp") && n_tcp < MAX_CHANNELS)
            tcp_specs[n_tcp++] = argv[arg + 1];
        else
            break;
        arg += 2;
    }
    if (arg >= argc)
    {
        fprintf(stderr, "usage: %s [--state DIR] [--tcp CHANNEL=PORT]... MODULE [COMMAND...]\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    setvbuf(stdout, NULL, _IOLBF, 0);
    mkdir_p(state_dir);

    static mhe_host host = {
        .size = sizeof(mhe_host),
        .log = host_log,
        .display_frame = host_display,
        .led_color = host_led,
        .serial_tx = host_serial,
    };
    host.state_dir = state_dir;

    char error[512];
    g_mod = mhe_load_module(argv[arg], &host, error, sizeof(error));
    if (! g_mod)
    {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    printf("device %s, firmware %s\n", g_mod->device, g_mod->firmware);

    for (size_t i = 0; i < g_mod->n_serials; ++i)
    {
        if (g_mod->serials[i].role == MHE_SERIAL_SYSTEM)
            g_system_channel = g_mod->serials[i].id;
        if (g_mod->serials[i].role == MHE_SERIAL_CLI)
            g_cli_channel = g_mod->serials[i].id;
    }
    for (int i = 0; i < n_tcp; ++i)
    {
        char name[32];
        unsigned port = 0;
        uint8_t channel;
        if (sscanf(tcp_specs[i], "%31[^=]=%u", name, &port) != 2 || ! port || port > 65535
            || ! find_channel(name, &channel) || channel >= MAX_CHANNELS)
        {
            fprintf(stderr, "bad --tcp %s\n", tcp_specs[i]);
            return 2;
        }
        g_tcp_ids[channel] = channel;
        g_tcp[channel] = mhe_tcp_serial_open((uint16_t)port, 0, tcp_rx, &g_tcp_ids[channel]);
        if (! g_tcp[channel])
            return 1;
        printf("channel %s on tcp://127.0.0.1:%u\n", name, port);
    }

    if (g_cli_channel >= 0)
        g_console = mhe_console_create(console_reply, NULL);
    if (g_system_channel >= 0 && ! g_tcp[g_system_channel])
    {
        g_sys = mhe_syscontrol_create(sys_reply, NULL, state_dir, getenv("MHE_SYS_DEBUG") != NULL);
        if (mhe_syscontrol_has_host(g_sys))
            printf("system-control: /sys_msgs ready, start mod-host now\n");
    }
    if (g_mod->start() != 0)
    {
        fprintf(stderr, "the firmware did not start\n");
        return 1;
    }
    if (g_console)
        mhe_console_boot(g_console);

    if (arg + 1 < argc)
    {
        for (int i = arg + 1; i < argc; ++i)
        {
            char line[LINE_MAX_LEN];
            snprintf(line, sizeof(line), "%s", argv[i]);
            if (run_command(line))
                break;
        }
    }
    else
    {
        char line[LINE_MAX_LEN];
        while (fgets(line, sizeof(line), stdin))
        {
            line[strcspn(line, "\r\n")] = 0;
            if (run_command(line))
                break;
        }
    }

    g_mod->stop();
    fflush(stdout);
    /* the firmware tasks are parked, not joined */
    _Exit(0);
}
