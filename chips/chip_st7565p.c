/*
 * mod-hw-emulator — ST7565P display controller model
 */

#include "chip_st7565p.h"
#include "../lpc177x/src/emu_core.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define RAM_PAGES   9
#define RAM_COLUMNS 132

struct st7565p_chip {
    st7565p_bus bus;
    uint16_t width, height;
    pthread_mutex_t mutex;

    uint8_t ram[RAM_PAGES][RAM_COLUMNS];
    uint8_t page, column;
    uint8_t start_line;
    uint8_t on, reverse, all_on, adc;
    uint8_t contrast;
    uint8_t expect;             /* second byte of a double byte command, 0 if none */

    uint32_t serial;            /* bumps on every write */
    uint32_t rendered_serial;
    uint8_t *last;              /* last rendered picture */
};

static void reset(st7565p_chip *c)
{
    c->page = c->column = c->start_line = 0;
    c->on = c->reverse = c->all_on = c->adc = 0;
    c->expect = 0;
    c->serial++;
}

static void command(st7565p_chip *c, uint8_t cmd)
{
    if (c->expect)
    {
        if (c->expect == 0x81)
            c->contrast = cmd & 0x3f;
        c->expect = 0;
        return;
    }

    if (cmd == 0xae || cmd == 0xaf)            c->on = cmd & 1;
    else if ((cmd & 0xc0) == 0x40)             c->start_line = cmd & 0x3f;
    else if ((cmd & 0xf0) == 0xb0)             c->page = cmd & 0x0f;
    else if ((cmd & 0xf0) == 0x10)             c->column = (uint8_t)((c->column & 0x0f) | ((cmd & 0x0f) << 4));
    else if ((cmd & 0xf0) == 0x00)             c->column = (uint8_t)((c->column & 0xf0) | (cmd & 0x0f));
    else if (cmd == 0xa0 || cmd == 0xa1)       c->adc = cmd & 1;
    else if (cmd == 0xa6 || cmd == 0xa7)       c->reverse = cmd & 1;
    else if (cmd == 0xa4 || cmd == 0xa5)       c->all_on = cmd & 1;
    else if (cmd == 0xe2)                      { reset(c); return; }
    else if (cmd == 0x81 || cmd == 0xac || cmd == 0xad || cmd == 0xf8) c->expect = cmd;
    /* bias, COM direction, power control, regulator ratio, read-modify-write, nop: no visible effect here */

    c->serial++;
}

static void data(st7565p_chip *c, uint8_t value)
{
    if (c->page < RAM_PAGES && c->column < RAM_COLUMNS)
        c->ram[c->page][c->column] = value;
    if (c->column < RAM_COLUMNS - 1)
        c->column++;
    c->serial++;
}

/* /WR rising edge latches D0..D7 */
static void on_wr(void *ctx, uint8_t port, uint32_t changed, uint32_t value)
{
    st7565p_chip *c = ctx;
    (void)port;

    const uint32_t wr = 1u << c->bus.wr_pin;
    if (! (changed & wr) || ! (value & wr))
        return;

    if ((emu_gpio_get(c->bus.cs_port) >> c->bus.cs_pin) & 1)
        return; /* not selected */

    const uint32_t d = emu_gpio_get(c->bus.data_port);
    uint8_t byte = 0;
    for (int i = 0; i < 8; ++i)
        if ((d >> c->bus.data_pins[i]) & 1)
            byte |= (uint8_t)(1u << i);

    const int is_data = (emu_gpio_get(c->bus.a0_port) >> c->bus.a0_pin) & 1;

    pthread_mutex_lock(&c->mutex);
    if (is_data)
        data(c, byte);
    else
        command(c, byte);
    pthread_mutex_unlock(&c->mutex);
}

/* /RST low resets the controller */
static void on_rst(void *ctx, uint8_t port, uint32_t changed, uint32_t value)
{
    st7565p_chip *c = ctx;
    (void)port;
    const uint32_t rst = 1u << c->bus.rst_pin;
    if ((changed & rst) && ! (value & rst))
    {
        pthread_mutex_lock(&c->mutex);
        reset(c);
        pthread_mutex_unlock(&c->mutex);
    }
}

st7565p_chip *st7565p_chip_create(const st7565p_bus *bus, uint16_t width, uint16_t height)
{
    st7565p_chip *c = calloc(1, sizeof(*c));
    if (! c)
        return NULL;

    c->bus = *bus;
    c->width = width;
    c->height = height;
    c->rendered_serial = (uint32_t)-1;
    pthread_mutex_init(&c->mutex, NULL);
    reset(c);

    emu_gpio_listen(bus->wr_port, 1u << bus->wr_pin, on_wr, c);
    emu_gpio_listen(bus->rst_port, 1u << bus->rst_pin, on_rst, c);
    return c;
}

int st7565p_chip_render(st7565p_chip *c, uint8_t *bits)
{
    const size_t stride = (c->width + 7u) / 8u;

    pthread_mutex_lock(&c->mutex);
    if (c->serial == c->rendered_serial)
    {
        pthread_mutex_unlock(&c->mutex);
        return 0;
    }
    c->rendered_serial = c->serial;

    const size_t size = stride * c->height;
    memset(bits, 0, size);
    for (uint16_t y = 0; y < c->height; ++y)
    {
        const unsigned line = (y + c->start_line) % 64u;
        const unsigned page = line / 8u, bit = line % 8u;

        for (uint16_t x = 0; x < c->width; ++x)
        {
            const unsigned column = c->adc ? (RAM_COLUMNS - 1u - x) : x;
            int lit = (c->ram[page][column] >> bit) & 1;
            if (c->all_on)
                lit = 1;
            if (c->reverse)
                lit = ! lit;
            if (! c->on)
                lit = 0;
            if (lit)
                bits[y * stride + x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
        }
    }
    pthread_mutex_unlock(&c->mutex);

    /* the firmware rewrites the whole RAM on every update: only a different picture is a change */
    if (! c->last)
        c->last = calloc(1, size);
    else if (memcmp(c->last, bits, size) == 0)
        return 0;
    if (c->last)
        memcpy(c->last, bits, size);
    return 1;
}

uint8_t st7565p_chip_contrast(const st7565p_chip *c)
{
    return c->contrast;
}
