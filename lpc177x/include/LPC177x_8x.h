/*
 * mod-hw-emulator — LPC177x_8x.h wrapper
 *
 * The firmware's own header (staged as LPC177x_8x_fw.h) gives the types, IRQ numbers and register layouts.
 * Peripheral pointers are only compared by the emulated driver API, never dereferenced, except those the
 * firmware reads as registers: they point to blocks the emulation keeps up to date.
 *
 *   LPC_ADC         the Duo X reads the potentiometers from LPC_ADC->DR[channel] (adc.c)
 *   LPC_IOCON_BASE  the Duo X writes the pin configuration of its analog inputs there (misc.c); it is
 *                   only kept, pins work as GPIO or as the peripheral the driver API sets
 */

#ifndef MHE_LPC177X_8X_WRAPPER_H
#define MHE_LPC177X_8X_WRAPPER_H

#include "LPC177x_8x_fw.h"

#ifdef __cplusplus
extern "C" {
#endif

extern LPC_ADC_TypeDef emu_adc_regs;
#undef LPC_ADC
#define LPC_ADC (&emu_adc_regs)

/* one 32 bit register per pin, 32 pins per port, ports 0..5 */
extern uint32_t emu_iocon_regs[6 * 32];
#undef LPC_IOCON_BASE
#define LPC_IOCON_BASE ((uintptr_t)emu_iocon_regs)

#ifdef __cplusplus
}
#endif

#endif
