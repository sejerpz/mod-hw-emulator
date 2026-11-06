#ifndef MHE_PNG_H
#define MHE_PNG_H

#include <stdint.h>

/* writes an 8 bit RGB image; returns 0 on success */
int png_write_rgb(const char *path, const uint8_t *rgb, unsigned width, unsigned height);

#endif
