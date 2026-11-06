/*
 * mod-hw-emulator — emulated Linux serial console of the device (the controller "CLI" line)
 */

#ifndef MHE_CONSOLE_H
#define MHE_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mhe_console mhe_console;

typedef void (*mhe_console_reply)(void *ctx, const uint8_t *data, size_t size);

mhe_console *mhe_console_create(mhe_console_reply reply, void *ctx);
void mhe_console_destroy(mhe_console *c);

/* plays the system boot messages and the login prompt, as after a power on; once the firmware runs */
void mhe_console_boot(mhe_console *c);

/* bytes the controller wrote on the console line */
void mhe_console_feed(mhe_console *c, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
