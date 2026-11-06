/*
 * mod-hw-emulator — replaces the CMSIS Cortex-M3 core header.
 * Only what the firmware and the NXP driver headers use: qualifiers, NVIC calls, core registers.
 */

#ifndef MHE_CORE_CM3_H
#define MHE_CORE_CM3_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define __I  volatile
#else
#define __I  volatile const
#endif
#define __O  volatile
#define __IO volatile

#define __ASM    __asm
#define __INLINE inline
#define __STATIC_INLINE static inline

void NVIC_EnableIRQ(IRQn_Type IRQn);
void NVIC_DisableIRQ(IRQn_Type IRQn);
void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority);
uint32_t NVIC_GetPriority(IRQn_Type IRQn);
void NVIC_SetPendingIRQ(IRQn_Type IRQn);
void NVIC_ClearPendingIRQ(IRQn_Type IRQn);
void NVIC_SystemReset(void);

static inline uint32_t __get_PSP(void) { return 0; }
static inline uint32_t __get_MSP(void) { return 0; }
static inline uint32_t __get_CONTROL(void) { return 0; }
static inline uint32_t __get_PRIMASK(void) { return 0; }

void __enable_irq(void);
void __disable_irq(void);

static inline void __NOP(void) {}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
static inline void __DMB(void) {}
static inline void __WFI(void) {}

#ifdef __cplusplus
}
#endif

#endif
