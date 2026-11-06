/*
 * mod-hw-emulator — FreeRTOS port for the host (see port.c)
 * Replaces the ARM_CM3 portmacro.h of the firmware.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short
#define portSTACK_TYPE  uintptr_t   /* a stack slot holds the task thread pointer */
#define portBASE_TYPE   long
#define portPOINTER_SIZE_TYPE uintptr_t

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#if (configUSE_16_BIT_TICKS == 1)
typedef uint16_t TickType_t;
#define portMAX_DELAY ((TickType_t)0xffff)
#else
typedef uint32_t TickType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define portTICK_TYPE_IS_ATOMIC 1
#endif

#define portSTACK_GROWTH      (-1)
#define portTICK_PERIOD_MS    ((TickType_t)1000 / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT    8

/* scheduler */
void vPortYield(void);
void vPortYieldWithinAPI(void);
#define portYIELD()                       vPortYield()
#define portYIELD_WITHIN_API()            vPortYieldWithinAPI()
#define portEND_SWITCHING_ISR(x)          do { if ((x) != pdFALSE) vPortYieldFromISR(); } while (0)
#define portYIELD_FROM_ISR(x)             portEND_SWITCHING_ISR(x)
void vPortYieldFromISR(void);

/* interrupt masking and critical sections */
void vPortDisableInterrupts(void);
void vPortEnableInterrupts(void);
void vPortEnterCritical(void);
void vPortExitCritical(void);
UBaseType_t uxPortSetInterruptMaskFromISR(void);
void vPortClearInterruptMaskFromISR(UBaseType_t mask);

#define portDISABLE_INTERRUPTS()                 vPortDisableInterrupts()
#define portENABLE_INTERRUPTS()                  vPortEnableInterrupts()
#define portENTER_CRITICAL()                     vPortEnterCritical()
#define portEXIT_CRITICAL()                      vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR()        uxPortSetInterruptMaskFromISR()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)     vPortClearInterruptMaskFromISR(x)

/* vTaskSuspendAll() starts with it: a task switched away by the watchdog parks before it suspends the
 * scheduler, or it would hold the scheduler while parked */
void vPortSoftwareBarrier(void);
#define portSOFTWARE_BARRIER()                   vPortSoftwareBarrier()

/* task deletion: the host thread of the task has to go too */
void vPortTaskDeleting(void *tcb);
void vPortCleanUpTCB(void *tcb);
#define portPRE_TASK_DELETE_HOOK(tcb, yield_pending)  vPortTaskDeleting(tcb)
#define portCLEAN_UP_TCB(tcb)                        vPortCleanUpTCB(tcb)

#define portTASK_FUNCTION_PROTO(vFunction, pvParameters) void vFunction(void *pvParameters)
#define portTASK_FUNCTION(vFunction, pvParameters)       void vFunction(void *pvParameters)

#define portNOP()

/* the generic task selection, the optimised one uses CLZ */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

#ifdef __cplusplus
}
#endif

#endif
