/*
 * mod-hw-emulator — LPC177x timer driver API (lpc177x_8x_timer.h)
 *
 * A running timer with an interrupt on a match that resets the counter fires every
 * (match + 1) * prescale microseconds, as the hardware does: TIMERn_IRQn is a periodic irq source.
 */

#include "emu_core.h"

/* the NXP driver library of the firmware, for its MCU family (LPC177x_8x, LPC17xx) */
#include "device.h"

#define N_TIMERS 4
#define PCLK_HZ  60000000u

typedef struct {
    uint64_t prescale_ns;
    uint32_t match;
    int int_on_match;
    int running;
} emu_timer;

static emu_timer g_timers[N_TIMERS];

static int timer_index(LPC_TIM_TypeDef *t)
{
    if (t == LPC_TIM0) return 0;
    if (t == LPC_TIM1) return 1;
    if (t == LPC_TIM2) return 2;
    if (t == LPC_TIM3) return 3;
    return -1;
}

static void update_source(int i)
{
    const emu_timer *t = &g_timers[i];
    uint64_t period_us = 0;

    if (t->running && t->int_on_match)
    {
        period_us = (t->prescale_ns * ((uint64_t)t->match + 1u) + 999u) / 1000u;
        if (! period_us)
            period_us = 1;
    }

    emu_irq_set_period(TIMER0_IRQn + i, period_us);
}

void TIM_Init(LPC_TIM_TypeDef *TIMx, TIM_MODE_OPT TimerCounterMode, void *TIM_ConfigStruct)
{
    const int i = timer_index(TIMx);
    if (i < 0 || TimerCounterMode != TIM_TIMER_MODE)
        return;

    const TIM_TIMERCFG_Type *cfg = TIM_ConfigStruct;
    if (cfg->PrescaleOption == TIM_PRESCALE_USVAL)
        g_timers[i].prescale_ns = (uint64_t)cfg->PrescaleValue * 1000u;
    else
        g_timers[i].prescale_ns = (uint64_t)cfg->PrescaleValue * 1000000000u / PCLK_HZ;
}

void TIM_DeInit(LPC_TIM_TypeDef *TIMx)
{
    const int i = timer_index(TIMx);
    if (i < 0)
        return;
    g_timers[i].running = 0;
    update_source(i);
}

void TIM_ConfigMatch(LPC_TIM_TypeDef *TIMx, TIM_MATCHCFG_Type *TIM_MatchConfigStruct)
{
    const int i = timer_index(TIMx);
    if (i < 0)
        return;
    g_timers[i].match = TIM_MatchConfigStruct->MatchValue;
    g_timers[i].int_on_match = TIM_MatchConfigStruct->IntOnMatch;
    update_source(i);
}

void TIM_Cmd(LPC_TIM_TypeDef *TIMx, FunctionalState NewState)
{
    const int i = timer_index(TIMx);
    if (i < 0)
        return;
    g_timers[i].running = NewState == ENABLE;
    update_source(i);
}

/* the handler only runs on a match */
FlagStatus TIM_GetIntStatus(LPC_TIM_TypeDef *TIMx, TIM_INT_TYPE IntFlag)
{
    (void)TIMx; (void)IntFlag;
    return SET;
}

void TIM_ClearIntPending(LPC_TIM_TypeDef *TIMx, TIM_INT_TYPE IntFlag)
{
    (void)TIMx; (void)IntFlag;
}
