/*
 * mod-hw-emulator — minimal PNG writer (RGB, stored deflate blocks), enough for display snapshots
 */

#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; ++n)
    {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc(uint32_t c, const uint8_t *buf, size_t len)
{
    c ^= 0xffffffffu;
    for (size_t i = 0; i < len; ++i)
        c = crc_table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t head[8];
    put32(head, len);
    memcpy(head + 4, type, 4);
    uint32_t c = crc(0, head + 4, 4);
    c = crc(c, data, len);
    uint8_t tail[4];
    put32(tail, c);
    return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(tail, 1, 4, f) == 4;
}

int png_write_rgb(const char *path, const uint8_t *rgb, unsigned width, unsigned height)
{
    if (! crc_table[1])
        crc_init();

    const size_t row = (size_t)width * 3u + 1u;
    const size_t raw_len = row * height;
    uint8_t *raw = malloc(raw_len);
    if (! raw)
        return -1;
    for (unsigned y = 0; y < height; ++y)
    {
        raw[y * row] = 0; /* filter: none */
        memcpy(&raw[y * row + 1], &rgb[(size_t)y * width * 3u], (size_t)width * 3u);
    }

    /* zlib stream of stored blocks */
    const size_t n_blocks = (raw_len + 65534u) / 65535u;
    const size_t z_len = 2u + raw_len + n_blocks * 5u + 4u;
    uint8_t *z = malloc(z_len);
    if (! z)
    {
        free(raw);
        return -1;
    }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t pos = 0; pos < raw_len || pos == 0;)
    {
        const size_t n = raw_len - pos > 65535u ? 65535u : raw_len - pos;
        z[o++] = pos + n >= raw_len ? 1 : 0;
        z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(&z[o], &raw[pos], n);
        for (size_t i = 0; i < n; ++i)
        {
            a = (a + raw[pos + i]) % 65521u;
            b = (b + a) % 65521u;
        }
        o += n;
        pos += n;
        if (! n)
            break;
    }
    put32(&z[o], (b << 16) | a);
    o += 4;

    FILE *f = fopen(path, "wb");
    int ok = f != NULL;
    if (ok)
    {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
        uint8_t ihdr[13];
        put32(ihdr, width);
        put32(ihdr + 4, height);
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) && chunk(f, "IDAT", z, (uint32_t)o)
             && chunk(f, "IEND", NULL, 0);
        ok = (fclose(f) == 0) && ok;
    }

    free(z);
    free(raw);
    return ok ? 0 : -1;
}
