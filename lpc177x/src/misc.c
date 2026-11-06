/*
 * mod-hw-emulator — LPC177x system, clock and pin selection API, linker symbols
 */

#include "emu_core.h"

#include "LPC177x_8x.h"
#include "system_LPC177x_8x.h"
#include "lpc177x_8x_clkpwr.h"
#include "lpc177x_8x_pinsel.h"

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

uint32_t CLKPWR_GetCLK(uint8_t ClkType)
{
    return ClkType == CLKPWR_CLKTYPE_CPU ? SystemCoreClock : PeripheralClock;
}

PINSEL_RET_CODE PINSEL_SetPinFunc(uint8_t portnum, uint8_t pinnum, uint8_t funcnum)
{
    (void)portnum; (void)pinnum; (void)funcnum;
    return PINSEL_RET_OK;
}

/* symbols of the firmware linker script (memory_map.h): the host heap needs none of them */
uint8_t _pvHeapStart[16];
uint8_t _pvHeapEnd[16];
uint8_t _vStackTop[16];
uint8_t __top_SRAM[16];
uint8_t __top_SRAM0[16];
uint32_t __stack_size;
