/*
 * mod-hw-emulator — emulated Linux serial console of the device (the controller "CLI" line)
 *
 * The controller follows the boot of the system on its console (U-Boot, kernel, login prompt), logs in as
 * user hmi to know the system is up, and from then on runs shell commands there (systemctl, wifi...). Until
 * it has logged in it ignores the user's inputs.
 * This console plays the boot messages, answers the login, then every command line with an empty output.
 * Commands known to need an answer can be added to g_answers.
 */

#include "console.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE 512

enum { BOOTING, WAIT_LOGIN, WAIT_PASSWORD, SHELL };

struct mhe_console {
    mhe_console_reply reply;
    void *ctx;
    pthread_mutex_t mutex;
    int state;
    char line[MAX_LINE];
    size_t line_len;
    pthread_t boot_thread;
    int boot_started;
};

/* command prefix -> output line */
static const struct { const char *prefix; const char *output; } g_answers[] = {
    { "systemctl is-active", "active" },
    { "systemctl is-enabled", "enabled" },
};

static void say(mhe_console *c, const char *text)
{
    c->reply(c->ctx, (const uint8_t *)text, strlen(text));
}

static void handle_line(mhe_console *c, const char *line)
{
    switch (c->state)
    {
    case BOOTING:
        /* the system is not up yet: nothing reads the console */
        break;

    case WAIT_LOGIN:
        if (! strcmp(line, "hmi") || ! strcmp(line, "root"))
        {
            c->state = WAIT_PASSWORD;
            say(c, "\r\nPassword: ");
        }
        else
        {
            say(c, "\r\n\r\nmoddwarf login: ");
        }
        break;

    case WAIT_PASSWORD:
        c->state = SHELL;
        say(c, "\r\nLast login: emulated\r\n$ ");
        break;

    default:
    {
        const char *output = "";
        for (size_t i = 0; i < sizeof(g_answers) / sizeof(g_answers[0]); ++i)
            if (! strncmp(line, g_answers[i].prefix, strlen(g_answers[i].prefix)))
                output = g_answers[i].output;

        char text[MAX_LINE + 8];
        snprintf(text, sizeof(text), "%s\r\n", output);
        say(c, text);
        break;
    }
    }
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* the boot as the console shows it, one message at a time so the controller sees each step */
static void *boot_messages(void *arg)
{
    mhe_console *c = arg;
    static const char *steps[] = {
        "\r\nU-Boot 2023.01-mod-emulator\r\n",
        "Hit any key to stop autoboot:  0 \r\n",
        "Starting kernel ...\r\n",
    };

    /* the controller is up long before U-Boot prints: give it the same head start */
    sleep_ms(1500);

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        sleep_ms(400);
        pthread_mutex_lock(&c->mutex);
        say(c, steps[i]);
        pthread_mutex_unlock(&c->mutex);
    }

    sleep_ms(1000);
    pthread_mutex_lock(&c->mutex);
    c->state = WAIT_LOGIN;
    say(c, "\r\nmoddwarf login: ");
    pthread_mutex_unlock(&c->mutex);
    return NULL;
}

void mhe_console_boot(mhe_console *c)
{
    if (c->boot_started)
        return;
    c->boot_started = 1;
    if (pthread_create(&c->boot_thread, NULL, boot_messages, c) == 0)
        pthread_detach(c->boot_thread);
}

mhe_console *mhe_console_create(mhe_console_reply reply, void *ctx)
{
    mhe_console *c = calloc(1, sizeof(*c));
    if (! c)
        return NULL;
    c->reply = reply;
    c->ctx = ctx;
    pthread_mutex_init(&c->mutex, NULL);
    return c;
}

void mhe_console_destroy(mhe_console *c)
{
    if (! c)
        return;
    pthread_mutex_destroy(&c->mutex);
    free(c);
}

void mhe_console_feed(mhe_console *c, const uint8_t *data, size_t size)
{
    pthread_mutex_lock(&c->mutex);
    for (size_t i = 0; i < size; ++i)
    {
        const char ch = (char)data[i];
        if (ch == '\n' || ch == '\r' || ch == 0)
        {
            c->line[c->line_len] = 0;
            /* a bare newline still gets an answer: the controller sends one to wake the console up */
            if (c->line_len || ch == '\n')
                handle_line(c, c->line);
            c->line_len = 0;
        }
        else if (c->line_len < MAX_LINE - 1)
        {
            c->line[c->line_len++] = ch;
        }
    }
    pthread_mutex_unlock(&c->mutex);
}
