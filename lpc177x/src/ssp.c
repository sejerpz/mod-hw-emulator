/*
 * mod-hw-emulator — LPC177x SSP driver API (lpc177x_8x_ssp.h)
 *
 * Master only, transmit only: a frame written with SSP_SendData() reaches the listeners of its SSP at once
 * (a chip model reads its chip select and data/command pins from the GPIO state), so the transmit FIFO is
 * always empty and the bus never busy. The Duo X drives its two UC1701 displays this way, polling for
 * "FIFO empty and not busy" after every byte.
 */

#include "emu_core.h"

#include "LPC177x_8x.h"
#include "lpc177x_8x_ssp.h"

#include <string.h>

#define MAX_LISTENERS 4

typedef struct {
    emu_ssp_listener fn;
    void *ctx;
} ssp_listener;

static ssp_listener g_listeners[EMU_SSP_COUNT][MAX_LISTENERS];

static int ssp_index(LPC_SSP_TypeDef *SSPx)
{
    if (SSPx == LPC_SSP0) return 0;
    if (SSPx == LPC_SSP1) return 1;
    if (SSPx == LPC_SSP2) return 2;
    return -1;
}

void emu_ssp_listen(uint8_t ssp, emu_ssp_listener fn, void *ctx)
{
    if (ssp >= EMU_SSP_COUNT)
        return;
    for (int i = 0; i < MAX_LISTENERS; ++i)
    {
        if (! g_listeners[ssp][i].fn)
        {
            g_listeners[ssp][i].fn = fn;
            g_listeners[ssp][i].ctx = ctx;
            return;
        }
    }
    emu_log(MHE_LOG_ERROR, "ssp%u: too many listeners", ssp);
}

void SSP_ConfigStructInit(SSP_CFG_Type *SSP_InitStruct)
{
    SSP_InitStruct->CPHA = SSP_CPHA_FIRST;
    SSP_InitStruct->CPOL = SSP_CPOL_HI;
    SSP_InitStruct->ClockRate = 1000000;
    SSP_InitStruct->Databit = SSP_DATABIT_8;
    SSP_InitStruct->Mode = SSP_MASTER_MODE;
    SSP_InitStruct->FrameFormat = SSP_FRAME_SPI;
}

void SSP_Init(LPC_SSP_TypeDef *SSPx, SSP_CFG_Type *SSP_ConfigStruct) { (void)SSPx; (void)SSP_ConfigStruct; }
void SSP_DeInit(LPC_SSP_TypeDef *SSPx)                               { (void)SSPx; }
void SSP_Cmd(LPC_SSP_TypeDef *SSPx, FunctionalState NewState)        { (void)SSPx; (void)NewState; }

void SSP_SendData(LPC_SSP_TypeDef *SSPx, uint16_t Data)
{
    emu_preemption_point();
    const int i = ssp_index(SSPx);
    if (i < 0)
        return;
    for (int l = 0; l < MAX_LISTENERS && g_listeners[i][l].fn; ++l)
        g_listeners[i][l].fn(g_listeners[i][l].ctx, Data);
}

uint16_t SSP_ReceiveData(LPC_SSP_TypeDef *SSPx)
{
    (void)SSPx;
    return 0;
}

FlagStatus SSP_GetStatus(LPC_SSP_TypeDef *SSPx, uint32_t FlagType)
{
    (void)SSPx;
    emu_preemption_point();
    const uint32_t status = SSP_SR_TFE | SSP_SR_TNF;   /* transmit FIFO empty (so not full), idle, nothing received */
    return (status & FlagType) ? SET : RESET;
}
