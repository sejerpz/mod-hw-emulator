/*
 * mod-hw-emulator — wraps the FreeRTOSConfig.h of the firmware and adjusts what the host port needs.
 * The build stages the firmware's own file as FreeRTOSConfig_fw.h next to this one.
 */

#ifndef MHE_FREERTOS_CONFIG_WRAPPER_H
#define MHE_FREERTOS_CONFIG_WRAPPER_H

#include "FreeRTOSConfig_fw.h"

/* the idle task gives the host CPU back and takes pending context switches (see port.c) */
#undef  configUSE_IDLE_HOOK
#define configUSE_IDLE_HOOK 1

/* the host allocator is the heap */
#undef  configAPPLICATION_ALLOCATED_HEAP
#define configAPPLICATION_ALLOCATED_HEAP 0

/* stack overflows are the host's business */
#undef  configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW 0

/* the firmware asks for SystemCoreClock, the host port does not need it */
#undef  configCPU_CLOCK_HZ
#define configCPU_CLOCK_HZ 120000000UL

#endif
