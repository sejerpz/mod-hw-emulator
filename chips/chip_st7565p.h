/*
 * mod-hw-emulator — ST7565P display controller model
 *
 * Listens to the 8080 parallel bus the firmware drives through GPIO (A0, /WR, /CS, /RST, D0..D7) and keeps
 * the display RAM as the chip does: commands, page/column addressing, display on/off, reverse, start line.
 * The UC1701 has the same command set on a SPI bus: st7565p_chip_create_spi() reads its bytes from an SSP.
 */

#ifndef MHE_CHIP_ST7565P_H
#define MHE_CHIP_ST7565P_H

#include <stdint.h>

typedef struct {
    uint8_t data_port;
    uint8_t data_pins[8];   /* D0..D7 */
    uint8_t a0_port, a0_pin;
    uint8_t wr_port, wr_pin;
    uint8_t cs_port, cs_pin;
    uint8_t rst_port, rst_pin;
} st7565p_bus;

typedef struct {
    uint8_t ssp;                /* SSP the chip listens to */
    uint8_t cd_port, cd_pin;    /* command (low) / data (high) */
    uint8_t cs_port, cs_pin;    /* selected when low */
    uint8_t rst_port, rst_pin;
} st7565p_spi_bus;

typedef struct st7565p_chip st7565p_chip;

st7565p_chip *st7565p_chip_create(const st7565p_bus *bus, uint16_t width, uint16_t height);
st7565p_chip *st7565p_chip_create_spi(const st7565p_spi_bus *bus, uint16_t width, uint16_t height);

/* renders the visible image as Mono1 rows ((width + 7) / 8 bytes each); returns 1 if it changed since the
 * last call */
int st7565p_chip_render(st7565p_chip *chip, uint8_t *bits);

uint8_t st7565p_chip_contrast(const st7565p_chip *chip);

#endif
