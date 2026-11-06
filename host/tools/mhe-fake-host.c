/*
 * mod-hw-emulator — plays mod-host on /sys_msgs, to try the widget path without jackd and mod-host
 *
 *   mhe-fake-host AREA_ID WIDTH HEIGHT [FRAMES]
 *
 * Writes widget bitmaps for AREA_ID exactly as mod-host's HMIWidgetsDrawBitmap does (a bouncing box and a
 * frame), one frame every 50 ms. The emulator (mhe-run or the UI) must be running: it creates /sys_msgs.
 * The area must exist on the controller (wa_add from mod-ui's Display dialog, or by hand with mhe-run:
 * send webgui wa_add PAGE SUBPAGE AREA_ID X Y WIDTH HEIGHT).
 */

#include <stdbool.h>
#include "sys_host.h"   /* mod-host's, client side */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void draw(sys_serial_shm_data *data, int area, int w, int h, int frame)
{
    const int stride = (w + 7) / 8;
    uint8_t bits[64 * 16];
    memset(bits, 0, sizeof(bits));

    /* frame */
    for (int x = 0; x < w; ++x)
    {
        bits[x / 8] |= 0x80 >> (x % 8);
        bits[(h - 1) * stride + x / 8] |= 0x80 >> (x % 8);
    }
    for (int y = 0; y < h; ++y)
    {
        bits[y * stride] |= 0x80;
        bits[y * stride + (w - 1) / 8] |= 0x80 >> ((w - 1) % 8);
    }

    /* a 6x6 box bouncing inside */
    const int range_x = w - 8, range_y = h - 8;
    int bx = frame % (2 * range_x), by = (frame * 2) % (2 * range_y);
    if (bx >= range_x) bx = 2 * range_x - bx;
    if (by >= range_y) by = 2 * range_y - by;
    for (int y = by + 1; y < by + 7; ++y)
        for (int x = bx + 1; x < bx + 7; ++x)
            bits[y * stride + x / 8] |= 0x80 >> (x % 8);

    /* the message of mod-host: "area x y width height HEXROWS" */
    char msg[2 * 1024 + 64];
    int len = snprintf(msg, sizeof(msg), "%i 0 0 %i %i ", area, w, h);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < stride * h; ++i)
    {
        msg[len++] = hex[bits[i] >> 4];
        msg[len++] = hex[bits[i] & 0xf];
    }
    msg[len] = 0;

    /* posts the semaphore itself, as for mod-host */
    sys_serial_write(&data->server, sys_serial_event_type_widget_bitmap, 0, 0, msg);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s AREA_ID WIDTH HEIGHT [FRAMES]\n", argv[0]);
        return 2;
    }
    const int area = atoi(argv[1]), w = atoi(argv[2]), h = atoi(argv[3]);
    const int frames = argc > 4 ? atoi(argv[4]) : 100;
    if (w < 9 || h < 9 || w > 128 || h > 64)
    {
        fprintf(stderr, "the area must be 9x9 to 128x64\n");
        return 2;
    }

    int fd;
    sys_serial_shm_data *data;
    if (! sys_serial_open(&fd, &data))
    {
        fprintf(stderr, "no /sys_msgs: start the emulator first\n");
        return 1;
    }

    for (int i = 0; i < frames; ++i)
    {
        draw(data, area, w, h, i);
        struct timespec ts = { 0, 50 * 1000000L };
        nanosleep(&ts, NULL);
    }

    sys_serial_close(fd, data);
    return 0;
}
