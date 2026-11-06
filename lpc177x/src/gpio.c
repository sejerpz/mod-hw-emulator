/*
 * mod-hw-emulator — LPC177x GPIO driver API (lpc177x_8x_gpio.h)
 *
 * Pins hold their output level and their direction; input pins read the level driven from outside
 * (buttons, encoders), high when nothing drives them (pull-ups). Listeners see output changes (chip models
 * decode their bus with them); meters measure the duty cycle of output pins (software PWM of the LEDs).
 */

#include "emu_core.h"

#include "LPC177x_8x.h"
#include "lpc177x_8x_gpio.h"

#include <pthread.h>
#include <stdatomic.h>

static _Atomic uint32_t g_out[EMU_GPIO_PORTS];
static _Atomic uint32_t g_dir[EMU_GPIO_PORTS];   /* 1 = output */
static _Atomic uint32_t g_in[EMU_GPIO_PORTS] = { 0xffffffffu, 0xffffffffu, 0xffffffffu,
                                                 0xffffffffu, 0xffffffffu, 0xffffffffu };

/* ---- listeners ---- */

#define MAX_LISTENERS 8

typedef struct {
    uint8_t port;
    uint32_t mask;
    emu_pin_listener fn;
    void *ctx;
} listener;

static listener g_listeners[MAX_LISTENERS];
static int g_n_listeners;
static uint32_t g_listen_mask[EMU_GPIO_PORTS];

void emu_gpio_listen(uint8_t port, uint32_t mask, emu_pin_listener fn, void *ctx)
{
    if (port >= EMU_GPIO_PORTS || g_n_listeners == MAX_LISTENERS)
        return;
    g_listeners[g_n_listeners++] = (listener){ port, mask, fn, ctx };
    g_listen_mask[port] |= mask;
}

/* ---- meters ---- */

#define MAX_METERS 32

typedef struct {
    uint8_t port, pin;
    int level;
    uint64_t since, acc, last_take;
} meter;

static meter g_meters[MAX_METERS];
static int g_n_meters;
static uint32_t g_meter_mask[EMU_GPIO_PORTS];
static pthread_mutex_t g_meter_mutex = PTHREAD_MUTEX_INITIALIZER;

int emu_gpio_meter_add(uint8_t port, uint8_t pin)
{
    if (port >= EMU_GPIO_PORTS || g_n_meters == MAX_METERS)
        return -1;

    const uint64_t now = emu_now_us();
    meter *m = &g_meters[g_n_meters];
    m->port = port;
    m->pin = pin;
    m->level = (atomic_load(&g_out[port]) >> pin) & 1;
    m->since = m->last_take = now;
    m->acc = 0;
    g_meter_mask[port] |= 1u << pin;
    return g_n_meters++;
}

uint16_t emu_gpio_meter_take(int index)
{
    if (index < 0 || index >= g_n_meters)
        return 0;

    pthread_mutex_lock(&g_meter_mutex);
    meter *m = &g_meters[index];
    const uint64_t now = emu_now_us();
    uint64_t high = m->acc + (m->level && now > m->since ? now - m->since : 0);
    const uint64_t window = now > m->last_take ? now - m->last_take : 0;
    m->acc = 0;
    m->since = m->last_take = now;
    pthread_mutex_unlock(&g_meter_mutex);

    if (! window)
        return m->level ? 65535u : 0u;
    if (high > window)
        high = window;
    return (uint16_t)(high * 65535u / window);
}

static void meters_update(uint8_t port, uint32_t changed, uint32_t value)
{
    pthread_mutex_lock(&g_meter_mutex);
    const uint64_t now = emu_now_us();
    for (int i = 0; i < g_n_meters; ++i)
    {
        meter *m = &g_meters[i];
        if (m->port != port || ! (changed & (1u << m->pin)))
            continue;
        if (m->level && now > m->since)
            m->acc += now - m->since;
        m->since = now;
        m->level = (value >> m->pin) & 1;
    }
    pthread_mutex_unlock(&g_meter_mutex);
}

/* ---- output changes ---- */

static void outputs_changed(uint8_t port, uint32_t old, uint32_t value)
{
    const uint32_t changed = old ^ value;
    if (! changed)
        return;

    if (changed & g_meter_mask[port])
        meters_update(port, changed, value);

    if (changed & g_listen_mask[port])
    {
        for (int i = 0; i < g_n_listeners; ++i)
        {
            const listener *l = &g_listeners[i];
            if (l->port == port && (changed & l->mask))
                l->fn(l->ctx, port, changed, value);
        }
    }
}

uint32_t emu_gpio_get(uint8_t port)
{
    if (port >= EMU_GPIO_PORTS)
        return 0;
    const uint32_t dir = atomic_load(&g_dir[port]);
    return (atomic_load(&g_out[port]) & dir) | (atomic_load(&g_in[port]) & ~dir);
}

void emu_gpio_drive(uint8_t port, uint8_t pin, int level)
{
    if (port >= EMU_GPIO_PORTS || pin > 31)
        return;
    if (level)
        atomic_fetch_or(&g_in[port], 1u << pin);
    else
        atomic_fetch_and(&g_in[port], ~(1u << pin));
}

/* ---- driver API ---- */

void GPIO_Init(void)
{
}

void GPIO_SetDir(uint8_t portNum, uint32_t bitValue, uint8_t dir)
{
    if (portNum >= EMU_GPIO_PORTS)
        return;
    if (dir)
        atomic_fetch_or(&g_dir[portNum], bitValue);
    else
        atomic_fetch_and(&g_dir[portNum], ~bitValue);
}

void GPIO_SetValue(uint8_t portNum, uint32_t bitValue)
{
    emu_preemption_point();
    if (portNum >= EMU_GPIO_PORTS)
        return;
    const uint32_t old = atomic_fetch_or(&g_out[portNum], bitValue);
    outputs_changed(portNum, old, old | bitValue);
}

void GPIO_ClearValue(uint8_t portNum, uint32_t bitValue)
{
    emu_preemption_point();
    if (portNum >= EMU_GPIO_PORTS)
        return;
    const uint32_t old = atomic_fetch_and(&g_out[portNum], ~bitValue);
    outputs_changed(portNum, old, old & ~bitValue);
}

void GPIO_OutputValue(uint8_t portNum, uint32_t bitMask, uint8_t value)
{
    if (value)
        GPIO_SetValue(portNum, bitMask);
    else
        GPIO_ClearValue(portNum, bitMask);
}

uint32_t GPIO_ReadValue(uint8_t portNum)
{
    emu_preemption_point();
    return emu_gpio_get(portNum);
}

void FIO_SetDir(uint8_t portNum, uint32_t bitValue, uint8_t dir)   { GPIO_SetDir(portNum, bitValue, dir); }
void FIO_SetValue(uint8_t portNum, uint32_t bitValue)               { GPIO_SetValue(portNum, bitValue); }
void FIO_ClearValue(uint8_t portNum, uint32_t bitValue)             { GPIO_ClearValue(portNum, bitValue); }
uint32_t FIO_ReadValue(uint8_t portNum)                             { return GPIO_ReadValue(portNum); }

void FIO_SetMask(uint8_t portNum, uint32_t bitValue, uint8_t maskValue)
{
    (void)portNum; (void)bitValue; (void)maskValue;
}
