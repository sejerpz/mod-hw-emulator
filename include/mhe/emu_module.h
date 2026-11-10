/*
 * mod-hw-emulator — firmware module ABI
 *
 * A firmware module is a shared library holding one controller firmware plus the emulation layer.
 * The host loads it, calls mhe_module_entry() and talks to it only through the two structs below.
 * Both start with their own size and are only ever appended: check `size` before using a later member.
 */

#ifndef MHE_EMU_MODULE_H
#define MHE_EMU_MODULE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MHE_MODULE_ENTRY "mhe_module_entry"
#define MHE_FIRMWARE_INFO "mhe_firmware_info"   /* optional; may be called before the entry */

/* pixel formats of display frames, same values as ALABS_HMI_PixelFormat */
typedef enum {
    MHE_PIXEL_MONO1 = 0  /* 1 bit per pixel, rows top to bottom, (w+7)/8 bytes per row, MSB leftmost */
} mhe_pixel_format;

typedef enum {
    MHE_LOG_ERROR = 0,
    MHE_LOG_WARNING,
    MHE_LOG_INFO,
    MHE_LOG_DEBUG,
    MHE_LOG_TRACE
} mhe_log_level;

/* role of a serial channel of the controller */
typedef enum {
    MHE_SERIAL_WEBGUI = 0,  /* HMI protocol with mod-ui */
    MHE_SERIAL_SYSTEM,      /* system commands with mod-system-control */
    MHE_SERIAL_CLI,         /* debug console */
    MHE_SERIAL_OTHER
} mhe_serial_role;

typedef enum {
    MHE_INPUT_ENCODER = 0,  /* rotary encoder with push button */
    MHE_INPUT_BUTTON,       /* push button */
    MHE_INPUT_FOOTSWITCH,   /* footswitch */
    MHE_INPUT_POT           /* potentiometer */
} mhe_input_kind;

typedef struct {
    uint8_t id;             /* display id */
    uint16_t width, height;
    mhe_pixel_format format;
} mhe_display_info;

typedef struct {
    uint8_t id;             /* id within its kind */
    mhe_input_kind kind;
    const char *name;       /* e.g. "Knob 1", "Footswitch A" */
} mhe_input_info;

typedef struct {
    uint8_t id;
    const char *name;
} mhe_led_info;

typedef struct {
    uint8_t id;             /* channel id, used by serial_rx()/serial_tx() */
    mhe_serial_role role;
    const char *name;
} mhe_serial_info;

/* which firmware sources the module was built from, as git saw them at build time; strings never NULL */
typedef struct {
    size_t size;
    const char *device;     /* e.g. "dwarf" */
    const char *describe;   /* git describe --tags --always [--dirty]: "stls18", "stls18-21-g9b9e9f7-dirty" */
    const char *tag;        /* nearest tag ("stls18"), or the abbreviated hash when no tag is reachable */
    const char *commit;     /* full commit hash */
    const char *date;       /* commit date, ISO 8601 */
} mhe_fw_info;

typedef const mhe_fw_info *(*mhe_fw_info_fn)(void);

/* what the host gives the module */
typedef struct mhe_host {
    size_t size;
    void *handle;

    void (*log)(void *handle, mhe_log_level level, const char *message);

    /* a new frame of a display: `bits` holds height rows in `format`; brightness 0..255 (backlight) */
    void (*display_frame)(void *handle, uint8_t display, uint16_t width, uint16_t height,
                          mhe_pixel_format format, const uint8_t *bits, uint8_t brightness);

    /* colour of an led changed, 0..255 per channel */
    void (*led_color)(void *handle, uint8_t led, uint8_t r, uint8_t g, uint8_t b);

    /* the firmware sent bytes on a serial channel */
    void (*serial_tx)(void *handle, uint8_t channel, const uint8_t *data, size_t size);

    /* directory where the module keeps its state (EEPROM image), never NULL */
    const char *state_dir;
} mhe_host;

/* what the module gives the host; every function may be called from any host thread */
typedef struct mhe_module {
    size_t size;
    const char *device;     /* e.g. "dwarf" */
    const char *firmware;   /* firmware version string */

    size_t n_displays;
    const mhe_display_info *displays;
    size_t n_inputs;
    const mhe_input_info *inputs;
    size_t n_leds;
    const mhe_led_info *leds;
    size_t n_serials;
    const mhe_serial_info *serials;

    /* boots the firmware; returns 0 on success */
    int (*start)(void);
    /* stops the firmware; the module cannot be started again in the same process */
    void (*stop)(void);

    /* encoder: steps > 0 clockwise; encoder/button/footswitch: pressed 1/0; pot: value 0..65535 */
    void (*encoder_turn)(uint8_t id, int steps);
    void (*input_press)(mhe_input_kind kind, uint8_t id, int pressed);
    void (*pot_value)(uint8_t id, uint16_t value);

    /* bytes received on a serial channel */
    void (*serial_rx)(uint8_t channel, const uint8_t *data, size_t size);

    /* the same as mhe_firmware_info(); may be NULL */
    const mhe_fw_info *info;
} mhe_module;

typedef const mhe_module *(*mhe_module_entry_fn)(const mhe_host *host);

#ifdef __cplusplus
}
#endif

#endif
