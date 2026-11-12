/*
 * mod-hw-emulator — LPC177x system, clock and pin selection API, linker symbols
 */

#include "emu_core.h"

/* the NXP driver library of the firmware, for its MCU family (LPC177x_8x, LPC17xx) */
#include "device.h"

uint32_t SystemCoreClock = 120000000u;
uint32_t PeripheralClock = 60000000u;
uint32_t EMCClock = 60000000u;

void SystemInit(void)
{
}

void SystemCoreClockUpdate(void)
{
}

void CLKPWR_ConfigPPWR(uint32_t PPType, FunctionalState NewState)
{
    (void)PPType; (void)NewState;
}

/* the pin configuration registers, for firmwares that write them directly (see LPC177x_8x.h) */
uint32_t emu_iocon_regs[6 * 32];

#ifdef LPC17xx
/* LPC17xx (MOD Duo): the peripheral clock is a fraction of the CPU clock, chosen per peripheral */
uint32_t CLKPWR_GetPCLK(uint32_t ClkType)
{
    (void)ClkType;
    return SystemCoreClock / 4;
}

void PINSEL_SetPinFunc(uint8_t portnum, uint8_t pinnum, uint8_t funcnum)
{
    if (portnum < 6 && pinnum < 32)
        emu_iocon_regs[portnum * 32 + pinnum] = funcnum & 3u;
}
#else
uint32_t CLKPWR_GetCLK(uint8_t ClkType)
{
    return ClkType == CLKPWR_CLKTYPE_CPU ? SystemCoreClock : PeripheralClock;
}

PINSEL_RET_CODE PINSEL_SetPinFunc(uint8_t portnum, uint8_t pinnum, uint8_t funcnum)
{
    if (portnum < 6 && pinnum < 32)
        emu_iocon_regs[portnum * 32 + pinnum] = (emu_iocon_regs[portnum * 32 + pinnum] & ~7u) | (funcnum & 7u);
    return PINSEL_RET_OK;
}
#endif

/* symbols of the firmware linker script (memory_map.h): the host heap needs none of them */
uint8_t _pvHeapStart[16];
uint8_t _pvHeapEnd[16];
uint8_t _vStackTop[16];
uint8_t __top_SRAM[16];
uint8_t __top_SRAM0[16];
uint32_t __stack_size;
