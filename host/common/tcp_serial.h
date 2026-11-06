/*
 * mod-hw-emulator — a serial line of the controller exposed on a TCP port
 *
 * The emulator listens (it is the device); one client at a time (mod-ui for the webgui line). Bytes from
 * the client go to the controller, bytes from the controller go to the client, dropped when none is
 * connected, as on a serial line with nobody at the other end.
 */

#ifndef MHE_TCP_SERIAL_H
#define MHE_TCP_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mhe_tcp_serial mhe_tcp_serial;

/* `rx` receives the bytes of the client, on the bridge thread */
typedef void (*mhe_tcp_rx)(void *ctx, const uint8_t *data, size_t size);

/* listens on 127.0.0.1:port (all interfaces if `any`), NULL on error */
mhe_tcp_serial *mhe_tcp_serial_open(uint16_t port, int any, mhe_tcp_rx rx, void *ctx);
void mhe_tcp_serial_close(mhe_tcp_serial *ts);

void mhe_tcp_serial_send(mhe_tcp_serial *ts, const uint8_t *data, size_t size);
int  mhe_tcp_serial_connected(const mhe_tcp_serial *ts);

#ifdef __cplusplus
}
#endif

#endif
