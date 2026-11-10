/*
 * mod-hw-emulator — LPC177x emulation core (internal)
 *
 * Execution model
 * - FreeRTOS tasks are host threads; only the thread of pxCurrentTCB runs (see port.c).
 * - Interrupt handlers run on one "interrupt controller" thread, never two at once.
 * - Masking interrupts (critical sections, portDISABLE_INTERRUPTS) takes the irq lock, which the interrupt
 *   thread holds while it runs a handler: a handler never runs inside a critical section, as on the MCU.
 * - Time is virtual, in microseconds, driven by the host clock; handlers that fell behind run in batches
 *   and see the virtual time they were due at.
 */

#ifndef EMU_CORE_H
#define EMU_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "mhe/emu_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- host link ---- */
extern const mhe_host *g_emu_host;
void emu_log(mhe_log_level level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* ---- virtual time ---- */
uint64_t emu_now_us(void);

/* ---- irq lock: "interrupts disabled" ---- */
void emu_irq_lock(void);          /* recursive */
void emu_irq_unlock(void);
int  emu_in_isr(void);

/* ---- interrupt controller ---- */
typedef void (*emu_isr_fn)(void);

/* irq numbers follow IRQn_Type of LPC177x_8x.h; >= EMU_IRQ_VIRTUAL are emulator sources */
#define EMU_IRQ_MAX        48
#define EMU_IRQ_VIRTUAL    40
#define EMU_IRQ_TICK       (EMU_IRQ_VIRTUAL + 0)
#define EMU_IRQ_BOARD0     (EMU_IRQ_VIRTUAL + 1)   /* free for the board: display refresh, led sampling… */
#define EMU_IRQ_BOARD1     (EMU_IRQ_VIRTUAL + 2)
#define EMU_IRQ_BOARD2     (EMU_IRQ_VIRTUAL + 3)

void emu_irq_set_handler(int irq, emu_isr_fn fn);
void emu_irq_enable(int irq, int enable);       /* NVIC enable */
void emu_irq_set_period(int irq, uint64_t period_us); /* periodic source, 0 = not periodic */
void emu_irq_pend(int irq);                      /* asynchronous request (e.g. uart data) */

int  emu_core_start(void);   /* starts the interrupt thread */
void emu_core_stop(void);

/* ---- preemption ---- */
/* called by every emulated hardware access from task context: takes a pending context switch */
void emu_preemption_point(void);
/* set by interrupts/tick when a higher priority task became ready */
void emu_request_switch(void);

/* ---- FreeRTOS port (port.c) ---- */
void emu_port_halt(void);                    /* stops scheduling: tasks park at their next point */
void emu_port_set_idle_yield(unsigned us);   /* host time given back by a task yielding to itself */
void emu_port_watchdog(void);                /* forces a switch the running task does not take */
void vPortDisableInterrupts(void);
void vPortEnableInterrupts(void);

/* ---- gpio (gpio.c) ---- */
#define EMU_GPIO_PORTS 6
typedef void (*emu_pin_listener)(void *ctx, uint8_t port, uint32_t changed, uint32_t value);
void     emu_gpio_listen(uint8_t port, uint32_t mask, emu_pin_listener fn, void *ctx);
uint32_t emu_gpio_get(uint8_t port);
/* input pins driven from outside (buttons, encoders) */
void     emu_gpio_drive(uint8_t port, uint8_t pin, int level);
/* duty cycle meter of an output pin: fraction of time high since the last call, 0..65535 */
int      emu_gpio_meter_add(uint8_t port, uint8_t pin);
uint16_t emu_gpio_meter_take(int meter);

/* ---- uart (uart.c) ---- */
#define EMU_UART_COUNT 4
void emu_uart_rx(uint8_t uart, const uint8_t *data, size_t size);
typedef void (*emu_uart_tx_fn)(uint8_t uart, const uint8_t *data, size_t size);
void emu_uart_set_tx(emu_uart_tx_fn fn);
void emu_uart_init(void);

/* ---- ssp (ssp.c): every frame sent goes to the listeners of its SSP ---- */
#define EMU_SSP_COUNT 3
typedef void (*emu_ssp_listener)(void *ctx, uint16_t data);
void emu_ssp_listen(uint8_t ssp, emu_ssp_listener fn, void *ctx);

/* ---- adc (adc.c) ---- */
#define EMU_ADC_CHANNELS 8
void emu_adc_set(uint8_t channel, uint16_t value);   /* 12 bit level of an input, 0..4095 */

/* ---- Cortex-M behaviours (arm_semantics.c) ---- */
void emu_arm_semantics_install(void);   /* integer division by zero returns 0 inside the module */

/* ---- eeprom (eeprom.c) ---- */
void emu_eeprom_set_file(const char *path);
void emu_eeprom_flush(void);   /* saves the pending writes */

#ifdef __cplusplus
}
#endif

#endif
