/*
 * mod-hw-emulator — LPC177x ADC driver API (lpc177x_8x_adc.h)
 *
 * Burst mode as the firmware leaves it: every enabled channel always holds a finished conversion of the
 * level the host gives it (a potentiometer). The data registers are a real block, emu_adc_regs, because the
 * Duo X reads LPC_ADC->DR[channel] directly: LPC177x_8x.h (the emulator's wrapper) points LPC_ADC to it.
 */

#include "emu_core.h"

#include "LPC177x_8x.h"
#include "lpc177x_8x_adc.h"

#include <stdatomic.h>

LPC_ADC_TypeDef emu_adc_regs;

/* 12 bit levels, mid travel until the host says otherwise */
static _Atomic uint16_t g_level[EMU_ADC_CHANNELS] = { 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048 };

static void convert(uint8_t channel)
{
    /* RESULT in bits 4..15, DONE in bit 31 */
    emu_adc_regs.DR[channel] = ADC_DR_DONE_FLAG | ((uint32_t)atomic_load(&g_level[channel]) << 4);
}

void emu_adc_set(uint8_t channel, uint16_t value)
{
    if (channel >= EMU_ADC_CHANNELS)
        return;
    atomic_store(&g_level[channel], (uint16_t)(value > 4095 ? 4095 : value));
    convert(channel);
}

void ADC_Init(LPC_ADC_TypeDef *ADCx, uint32_t rate)
{
    (void)ADCx; (void)rate;
    for (uint8_t c = 0; c < EMU_ADC_CHANNELS; ++c)
        convert(c);
}

void ADC_DeInit(LPC_ADC_TypeDef *ADCx)                                          { (void)ADCx; }
void ADC_BurstCmd(LPC_ADC_TypeDef *ADCx, FunctionalState NewState)              { (void)ADCx; (void)NewState; }
void ADC_PowerdownCmd(LPC_ADC_TypeDef *ADCx, FunctionalState NewState)          { (void)ADCx; (void)NewState; }
void ADC_StartCmd(LPC_ADC_TypeDef *ADCx, uint8_t start_mode)                    { (void)ADCx; (void)start_mode; }
void ADC_ChannelCmd(LPC_ADC_TypeDef *ADCx, uint8_t Channel, FunctionalState NewState)
{
    (void)ADCx; (void)Channel; (void)NewState;
}
void ADC_EdgeStartConfig(LPC_ADC_TypeDef *ADCx, uint8_t EdgeOption)             { (void)ADCx; (void)EdgeOption; }
void ADC_IntConfig(LPC_ADC_TypeDef *ADCx, ADC_TYPE_INT_OPT IntType, FunctionalState NewState)
{
    (void)ADCx; (void)IntType; (void)NewState;
}

uint16_t ADC_ChannelGetData(LPC_ADC_TypeDef *ADCx, uint8_t channel)
{
    (void)ADCx;
    emu_preemption_point();
    return channel < EMU_ADC_CHANNELS ? atomic_load(&g_level[channel]) : 0;
}

FlagStatus ADC_ChannelGetStatus(LPC_ADC_TypeDef *ADCx, uint8_t channel, uint32_t StatusType)
{
    (void)ADCx;
    emu_preemption_point();
    /* StatusType 1: done (always, burst mode); 0: overrun (never) */
    return channel < EMU_ADC_CHANNELS && StatusType ? SET : RESET;
}

uint32_t ADC_GlobalGetData(LPC_ADC_TypeDef *ADCx)
{
    (void)ADCx;
    return emu_adc_regs.DR[0];
}

FlagStatus ADC_GlobalGetStatus(LPC_ADC_TypeDef *ADCx, uint32_t StatusType)
{
    (void)ADCx;
    return StatusType ? SET : RESET;
}
