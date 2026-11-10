/*
 * mod-hw-emulator — emulated mod-system-control
 *
 * Controller side: answers the system commands the controller sends on its system serial line
 * ("sys_* LEN ARGS"), keeping the values it is given, as mod-system-control does on the device with the audio
 * codec, Bluetooth, systemd...
 *
 * mod-host side: built with mod-system-control's own sys_host.c, it creates the /sys_msgs shared memory mod-host
 * opens when its HMI control is enabled, or listens for mod-host on MOD_SYS_SERIAL_PORT (always on Windows),
 * and forwards labels, values, leds and plugin widget
 * bitmaps to the controller with the same page cache and resends as on the device.
 *
 * Everything runs on one thread, in the order of mod-system-control's main loop.
 */

#ifndef MHE_SYSCONTROL_H
#define MHE_SYSCONTROL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mhe_syscontrol mhe_syscontrol;

/* `reply` sends bytes to the controller system serial */
typedef void (*mhe_syscontrol_reply)(void *ctx, const uint8_t *data, size_t size);

/* `state_dir` keeps the values across runs; `debug` prints the traffic with mod-host */
mhe_syscontrol *mhe_syscontrol_create(mhe_syscontrol_reply reply, void *ctx, const char *state_dir, int debug);
void mhe_syscontrol_destroy(mhe_syscontrol *sc);

/* bytes the controller sent on its system serial; may hold partial or several messages */
void mhe_syscontrol_feed(mhe_syscontrol *sc, const uint8_t *data, size_t size);

/* 1 if the mod-host side is there (shared memory created) */
int mhe_syscontrol_has_host(const mhe_syscontrol *sc);

#ifdef __cplusplus
}
#endif

#endif
