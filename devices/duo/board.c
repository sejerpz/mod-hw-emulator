/*
 * mod-hw-emulator — MOD Duo board
 *
 * Binds the firmware pins to the chip models and to the inputs, using the firmware's own config-modduo.h,
 * and exposes the firmware module to the host: two UC1701 displays on SSP0, two encoders with a push, two
 * footswitches, two RGB leds. The Duo controller is an LPC1759 (LPC17xx driver library) with two serial
 * lines, mod-ui (webgui) and the Linux console (cli): no mod-system-control line and no EEPROM.
 */

#include "config.h"
#include "device.h"

#include "emu_core.h"
#include "chip_st7565p.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the firmware, see the build adaptations in the Makefile */
extern int fw_main(void);
extern void TIMER0_IRQHandler(void);
extern void TIMER1_IRQHandler(void);
extern void UART0_IRQHandler(void);
extern void UART1_IRQHandler(void);
extern void emu_uart_after_isr(int uart);
extern void emu_port_halt(void);

#define EXPORT __attribute__((visibility("default")))

/* ---------------------------------------------------------------------------------------------------------------
 * panel
 */

#define DISPLAY_W 128
#define DISPLAY_H 64

static const mhe_display_info g_displays[] = {
    { 0, DISPLAY_W, DISPLAY_H, MHE_PIXEL_MONO1 },   /* left */
    { 1, DISPLAY_W, DISPLAY_H, MHE_PIXEL_MONO1 },   /* right */
};

/* one slot per side: display, knob, footswitch, led; the names are those of the skin (devices/duo/skin) */
static const mhe_input_info g_inputs[] = {
    { 0, MHE_INPUT_ENCODER,    "Knob 1" },
    { 1, MHE_INPUT_ENCODER,    "Knob 2" },
    { 0, MHE_INPUT_FOOTSWITCH, "Footswitch 1" },
    { 1, MHE_INPUT_FOOTSWITCH, "Footswitch 2" },
};

static const mhe_led_info g_leds[] = {
    { 0, "Footswitch 1" },
    { 1, "Footswitch 2" },
};

static const mhe_serial_info g_serials[] = {
    { WEBGUI_SERIAL, MHE_SERIAL_WEBGUI, "webgui" },
    { CLI_SERIAL,    MHE_SERIAL_CLI,    "cli" },
};

#define N_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

/* ---------------------------------------------------------------------------------------------------------------
 * pins, from config-modduo.h
 */

/* the fields of the firmware's uc1701_t that GLCDn_CONFIG fills */
typedef struct {
    void *ssp_module;
    uint32_t ssp_clock;
    uint8_t ssp_clk_port, ssp_clk_pin, ssp_clk_func;
    uint8_t ssp_mosi_port, ssp_mosi_pin, ssp_mosi_func;
    uint8_t cs_port, cs_pin;
    uint8_t cd_port, cd_pin;
    uint8_t rst_port, rst_pin;
    uint8_t backlight_port, backlight_pin;
} glcd_pins;

static const glcd_pins g_glcd_pins[GLCD_COUNT] = { GLCD0_CONFIG GLCD1_CONFIG };

static const uint8_t g_encoder_pins[ENCODERS_COUNT][6] = { ENCODER0_PINS, ENCODER1_PINS };
static const uint8_t g_footswitch_pins[FOOTSWITCHES_COUNT][2] = { FOOTSWITCH0_PINS, FOOTSWITCH1_PINS };
static const int g_led_pins[LEDS_COUNT][6] = { LED0_PINS, LED1_PINS };

/* ---------------------------------------------------------------------------------------------------------------
 * state
 */

static st7565p_chip *g_display[GLCD_COUNT];
static int g_backlight_meter[GLCD_COUNT];
/* one frame per display: a brightness change alone sends the frame of that display again */
static uint8_t g_frame[GLCD_COUNT][DISPLAY_H * ((DISPLAY_W + 7) / 8)];
static uint8_t g_brightness[GLCD_COUNT];
static int g_frame_sent[GLCD_COUNT];

static int g_led_meters[LEDS_COUNT][3];
static uint8_t g_led_color[LEDS_COUNT][3];
static int g_led_sent[LEDS_COUNT];

typedef struct {
    atomic_int pending;   /* quadrature transitions to emit, > 0 clockwise */
    int state;            /* A | B << 1 */
} encoder_drive;

static encoder_drive g_encoders[ENCODERS_COUNT];

static pthread_t g_fw_thread;
static int g_started;

/* ---------------------------------------------------------------------------------------------------------------
 * periodic board work, on the interrupt thread
 */

/* display refresh: each panel shows what its controller RAM holds */
static void display_refresh(void)
{
    for (uint8_t d = 0; d < GLCD_COUNT; ++d)
    {
        const uint16_t duty = emu_gpio_meter_take(g_backlight_meter[d]);
        /* in 1/16 steps: the Duo dims its backlight by software PWM (TIMER0), sampled here with some jitter */
        const uint8_t brightness = (uint8_t)(((duty + 2048u) >> 12) * 255u / 16u);
        const int changed = st7565p_chip_render(g_display[d], g_frame[d]);

        if (changed || brightness != g_brightness[d] || ! g_frame_sent[d])
        {
            g_brightness[d] = brightness;
            g_frame_sent[d] = 1;
            if (g_emu_host && g_emu_host->display_frame)
                g_emu_host->display_frame(g_emu_host->handle, d, DISPLAY_W, DISPLAY_H, MHE_PIXEL_MONO1,
                                          g_frame[d], brightness);
        }
    }
}

/* leds: the firmware does software PWM, the colour is the duty cycle of each pin; they light on a low pin
 * (LED_TURN_ON_WITH_ZERO in led.h) */
static void leds_sample(void)
{
    for (int i = 0; i < LEDS_COUNT; ++i)
    {
        uint8_t rgb[3];
        for (int c = 0; c < 3; ++c)
            rgb[c] = (uint8_t)(255 - (emu_gpio_meter_take(g_led_meters[i][c]) >> 8));

        if (! g_led_sent[i] || memcmp(rgb, g_led_color[i], 3) != 0)
        {
            memcpy(g_led_color[i], rgb, 3);
            g_led_sent[i] = 1;
            if (g_emu_host && g_emu_host->led_color)
                g_emu_host->led_color(g_emu_host->handle, (uint8_t)i, rgb[0], rgb[1], rgb[2]);
        }
    }
}

/* encoders: one quadrature transition per period, slower than the 1 ms actuator scan */
static void encoders_step(void)
{
    /* clockwise for the firmware decoder: 0 -> 1 -> 3 -> 2 -> 0 */
    static const int cw[4] = { 1, 3, 0, 2 };
    static const int ccw[4] = { 2, 0, 3, 1 };

    for (int i = 0; i < ENCODERS_COUNT; ++i)
    {
        encoder_drive *e = &g_encoders[i];
        const int pending = atomic_load(&e->pending);
        if (! pending)
            continue;

        if (pending > 0)
        {
            e->state = cw[e->state];
            atomic_fetch_sub(&e->pending, 1);
        }
        else
        {
            e->state = ccw[e->state];
            atomic_fetch_add(&e->pending, 1);
        }

        emu_gpio_drive(g_encoder_pins[i][2], g_encoder_pins[i][3], e->state & 1);
        emu_gpio_drive(g_encoder_pins[i][4], g_encoder_pins[i][5], (e->state >> 1) & 1);
    }
}

static void uart0_isr(void) { UART0_IRQHandler(); emu_uart_after_isr(0); }
static void uart1_isr(void) { UART1_IRQHandler(); emu_uart_after_isr(1); }

static void serial_tx(uint8_t uart, const uint8_t *data, size_t size)
{
    if (g_emu_host && g_emu_host->serial_tx)
        g_emu_host->serial_tx(g_emu_host->handle, uart, data, size);
}

/* ---------------------------------------------------------------------------------------------------------------
 * module interface
 */

static void *fw_thread(void *arg)
{
    (void)arg;
    fw_main();
    return NULL;
}

static uint8_t ssp_of(const void *module)
{
    return module == (const void *)LPC_SSP1 ? 1 : 0;
}

static int board_start(void)
{
    if (g_started)
        return -1;
    g_started = 1;

    emu_arm_semantics_install();

    emu_uart_init();
    emu_uart_set_tx(serial_tx);

    /* displays: UC1701 on SPI, one chip select each */
    for (int d = 0; d < GLCD_COUNT; ++d)
    {
        const glcd_pins *p = &g_glcd_pins[d];
        const st7565p_spi_bus bus = {
            .ssp = ssp_of(p->ssp_module),
            .cd_port = p->cd_port, .cd_pin = p->cd_pin,
            .cs_port = p->cs_port, .cs_pin = p->cs_pin,
            .rst_port = p->rst_port, .rst_pin = p->rst_pin,
        };
        g_display[d] = st7565p_chip_create_spi(&bus, DISPLAY_W, DISPLAY_H);
        g_backlight_meter[d] = emu_gpio_meter_add(p->backlight_port, p->backlight_pin);
    }

    for (int i = 0; i < LEDS_COUNT; ++i)
        for (int c = 0; c < 3; ++c)
            g_led_meters[i][c] = emu_gpio_meter_add((uint8_t)g_led_pins[i][c * 2], (uint8_t)g_led_pins[i][c * 2 + 1]);

    for (int i = 0; i < ENCODERS_COUNT; ++i)
        g_encoders[i].state = 3; /* both channels high: pull-ups */

    /* interrupts of the firmware */
    emu_irq_set_handler(TIMER0_IRQn, TIMER0_IRQHandler);
    emu_irq_set_handler(TIMER1_IRQn, TIMER1_IRQHandler);
    emu_irq_set_handler(UART0_IRQn, uart0_isr);
    emu_irq_set_handler(UART1_IRQn, uart1_isr);

    /* board work */
    emu_irq_set_handler(EMU_IRQ_BOARD0, display_refresh);
    emu_irq_set_period(EMU_IRQ_BOARD0, 1000000u / 30u);
    emu_irq_enable(EMU_IRQ_BOARD0, 1);
    emu_irq_set_handler(EMU_IRQ_BOARD1, leds_sample);
    emu_irq_set_period(EMU_IRQ_BOARD1, 20000u);
    emu_irq_enable(EMU_IRQ_BOARD1, 1);
    emu_irq_set_handler(EMU_IRQ_BOARD2, encoders_step);
    emu_irq_set_period(EMU_IRQ_BOARD2, 2000u);
    emu_irq_enable(EMU_IRQ_BOARD2, 1);

    if (emu_core_start() != 0)
        return -1;

    if (pthread_create(&g_fw_thread, NULL, fw_thread, NULL) != 0)
        return -1;
    pthread_detach(g_fw_thread);

    emu_log(MHE_LOG_INFO, "duo firmware %s (%s) started", MHE_FW_DESCRIBE, VERSION_HASH);
    return 0;
}

static void board_stop(void)
{
    emu_port_halt();
    emu_core_stop();
}

static void board_encoder_turn(uint8_t id, int steps)
{
    if (id < ENCODERS_COUNT)
        atomic_fetch_add(&g_encoders[id].pending, steps * 4); /* ENCODER_STEPS transitions per detent */
}

static void board_input_press(mhe_input_kind kind, uint8_t id, int pressed)
{
    const uint8_t *pins = NULL;

    if (kind == MHE_INPUT_ENCODER && id < ENCODERS_COUNT)
        pins = g_encoder_pins[id];
    else if (kind == MHE_INPUT_FOOTSWITCH && id < FOOTSWITCHES_COUNT)
        pins = g_footswitch_pins[id];

    /* active low (BUTTON_ACTIVATED, ENCODER_ACTIVATED) */
    if (pins)
        emu_gpio_drive(pins[0], pins[1], ! pressed);
}

static void board_serial_rx(uint8_t channel, const uint8_t *data, size_t size)
{
    emu_uart_rx(channel, data, size);
}

/* the git description of the firmware sources, given by the Makefile */
static const mhe_fw_info g_firmware_info = {
    .size = sizeof(mhe_fw_info),
    .device = "duo",
    .describe = MHE_FW_DESCRIBE,
    .tag = MHE_FW_TAG,
    .commit = MHE_FW_COMMIT,
    .date = MHE_FW_DATE,
};

static const mhe_module g_module = {
    .size = sizeof(mhe_module),
    .device = "duo",
    .firmware = VERSION_HASH,
    .n_displays = N_ELEMS(g_displays), .displays = g_displays,
    .n_inputs = N_ELEMS(g_inputs), .inputs = g_inputs,
    .n_leds = N_ELEMS(g_leds), .leds = g_leds,
    .n_serials = N_ELEMS(g_serials), .serials = g_serials,
    .start = board_start,
    .stop = board_stop,
    .encoder_turn = board_encoder_turn,
    .input_press = board_input_press,
    .serial_rx = board_serial_rx,
    .info = &g_firmware_info,
};

EXPORT const mhe_fw_info *mhe_firmware_info(void)
{
    return &g_firmware_info;
}

EXPORT const mhe_module *mhe_module_entry(const mhe_host *host)
{
    if (! host || host->size < sizeof(mhe_host))
        return NULL;
    g_emu_host = host;
    return &g_module;
}

/* configUSE_MALLOC_FAILED_HOOK: the firmware's hook reads ARM registers, it is cut out of the build */
void vApplicationMallocFailedHook(void)
{
    emu_log(MHE_LOG_ERROR, "firmware out of memory");
    abort();
}
