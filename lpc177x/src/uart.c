/*
 * mod-hw-emulator — LPC177x UART driver API (lpc177x_8x_uart.h)
 *
 * Receive: bytes from the host wait in a line buffer and move into a 16 byte FIFO as if they had just
 * arrived. The interrupt identification follows the hardware: "receive data available" while the FIFO
 * holds at least the trigger level, "character time-out" when fewer bytes are left and the line is idle.
 * Transmit: what the firmware writes leaves at once; the transmit holding register is always empty, so an
 * enabled THRE interrupt is always pending.
 */

#include "emu_core.h"

/* the NXP driver library of the firmware, for its MCU family (LPC177x_8x, LPC17xx) */
#include "device.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define FIFO_SIZE 16
#define LINE_SIZE 65536

typedef struct {
    pthread_mutex_t mutex;
    uint8_t fifo[FIFO_SIZE];
    size_t fifo_count;
    uint8_t *line;              /* ring buffer of bytes not yet in the fifo */
    size_t line_head, line_count;
    int trigger;
    int int_rbr, int_thre, int_rls;
    int tx_enabled;
} emu_uart;

static emu_uart g_uarts[EMU_UART_COUNT];
static emu_uart_tx_fn g_tx;

static const int g_irqs[EMU_UART_COUNT] = { UART0_IRQn, UART1_IRQn, UART2_IRQn, UART3_IRQn };

void emu_uart_init(void)
{
    for (int i = 0; i < EMU_UART_COUNT; ++i)
    {
        pthread_mutex_init(&g_uarts[i].mutex, NULL);
        g_uarts[i].line = calloc(1, LINE_SIZE);
        g_uarts[i].trigger = 1;
    }
}

void emu_uart_set_tx(emu_uart_tx_fn fn)
{
    g_tx = fn;
}

static int uart_index(LPC_UART_TypeDef *u)
{
    if (u == (LPC_UART_TypeDef *)LPC_UART0) return 0;
    if (u == (LPC_UART_TypeDef *)LPC_UART1) return 1;
    if (u == (LPC_UART_TypeDef *)LPC_UART2) return 2;
    if (u == (LPC_UART_TypeDef *)LPC_UART3) return 3;
    return -1;
}

/* moves line bytes into the fifo; mutex held */
static void refill(emu_uart *u)
{
    while (u->fifo_count < FIFO_SIZE && u->line_count)
    {
        u->fifo[u->fifo_count++] = u->line[u->line_head];
        u->line_head = (u->line_head + 1) % LINE_SIZE;
        u->line_count--;
    }
}

static int rx_interrupt_pending(emu_uart *u)
{
    return u->int_rbr && u->fifo_count > 0;
}

static void update_irq(int i)
{
    emu_uart *u = &g_uarts[i];
    pthread_mutex_lock(&u->mutex);
    const int pending = rx_interrupt_pending(u) || (u->int_thre && u->tx_enabled);
    pthread_mutex_unlock(&u->mutex);
    if (pending)
        emu_irq_pend(g_irqs[i]);
}

void emu_uart_rx(uint8_t uart, const uint8_t *data, size_t size)
{
    if (uart >= EMU_UART_COUNT)
        return;

    emu_uart *u = &g_uarts[uart];
    pthread_mutex_lock(&u->mutex);
    for (size_t n = 0; n < size; ++n)
    {
        if (u->line_count == LINE_SIZE)
        {
            emu_log(MHE_LOG_WARNING, "uart%u: receive overrun, %zu bytes dropped", uart, size - n);
            break;
        }
        u->line[(u->line_head + u->line_count) % LINE_SIZE] = data[n];
        u->line_count++;
    }
    refill(u);
    pthread_mutex_unlock(&u->mutex);

    update_irq(uart);
}

void UART_Init(LPC_UART_TypeDef *UARTx, UART_CFG_Type *UART_ConfigStruct)
{
    (void)UARTx; (void)UART_ConfigStruct;
}

void UART_DeInit(LPC_UART_TypeDef *UARTx)
{
    (void)UARTx;
}

void UART_ConfigStructInit(UART_CFG_Type *UART_InitStruct)
{
    memset(UART_InitStruct, 0, sizeof(*UART_InitStruct));
    UART_InitStruct->Baud_rate = 115200;
    UART_InitStruct->Databits = UART_DATABIT_8;
    UART_InitStruct->Parity = UART_PARITY_NONE;
    UART_InitStruct->Stopbits = UART_STOPBIT_1;
}

void UART_FIFOConfigStructInit(UART_FIFO_CFG_Type *UART_FIFOInitStruct)
{
    memset(UART_FIFOInitStruct, 0, sizeof(*UART_FIFOInitStruct));
    UART_FIFOInitStruct->FIFO_ResetRxBuf = ENABLE;
    UART_FIFOInitStruct->FIFO_ResetTxBuf = ENABLE;
    UART_FIFOInitStruct->FIFO_Level = UART_FIFO_TRGLEV0;
}

void UART_FIFOConfig(LPC_UART_TypeDef *UARTx, UART_FIFO_CFG_Type *FIFOCfg)
{
    const int i = uart_index(UARTx);
    if (i < 0)
        return;
    static const int levels[] = { 1, 4, 8, 14 };
    g_uarts[i].trigger = levels[FIFOCfg->FIFO_Level & 3];
}

void UART_TxCmd(LPC_UART_TypeDef *UARTx, FunctionalState NewState)
{
    const int i = uart_index(UARTx);
    if (i < 0)
        return;
    g_uarts[i].tx_enabled = NewState == ENABLE;
}

void UART_IntConfig(LPC_UART_TypeDef *UARTx, UART_INT_Type UARTIntCfg, FunctionalState NewState)
{
    emu_preemption_point();

    const int i = uart_index(UARTx);
    if (i < 0)
        return;

    emu_uart *u = &g_uarts[i];
    const int enable = NewState == ENABLE;
    switch (UARTIntCfg)
    {
    case UART_INTCFG_RBR:  u->int_rbr = enable; break;
    case UART_INTCFG_THRE: u->int_thre = enable; break;
    case UART_INTCFG_RLS:  u->int_rls = enable; break;
    default: break;
    }

    if (enable)
        update_irq(i);
}

uint32_t UART_GetIntId(LPC_UART_TypeDef *UARTx)
{
    const int i = uart_index(UARTx);
    if (i < 0)
        return UART_IIR_INTSTAT_PEND;

    emu_uart *u = &g_uarts[i];
    uint32_t id = UART_IIR_INTSTAT_PEND; /* bit set: no interrupt pending */

    pthread_mutex_lock(&u->mutex);
    refill(u);
    if (u->int_rbr && u->fifo_count >= (size_t)u->trigger && u->fifo_count > 1)
        id = UART_IIR_INTID_RDA;
    else if (u->int_rbr && u->fifo_count > 0)
        id = u->line_count ? UART_IIR_INTID_RDA : UART_IIR_INTID_CTI;
    else if (u->int_thre && u->tx_enabled)
        id = UART_IIR_INTID_THRE;
    pthread_mutex_unlock(&u->mutex);

    return id;
}

uint8_t UART_GetLineStatus(LPC_UART_TypeDef *UARTx)
{
    const int i = uart_index(UARTx);
    if (i < 0)
        return 0;

    emu_uart *u = &g_uarts[i];
    pthread_mutex_lock(&u->mutex);
    const uint8_t status = UART_LSR_THRE | UART_LSR_TEMT | (u->fifo_count ? UART_LSR_RDR : 0);
    pthread_mutex_unlock(&u->mutex);
    return status;
}

FlagStatus UART_CheckBusy(LPC_UART_TypeDef *UARTx)
{
    (void)UARTx;
    emu_preemption_point();
    return RESET;
}

uint32_t UART_Receive(LPC_UART_TypeDef *UARTx, uint8_t *rxbuf, uint32_t buflen, TRANSFER_BLOCK_Type flag)
{
    (void)flag;
    const int i = uart_index(UARTx);
    if (i < 0)
        return 0;

    emu_uart *u = &g_uarts[i];
    pthread_mutex_lock(&u->mutex);
    uint32_t n = 0;
    while (n < buflen && u->fifo_count)
    {
        rxbuf[n++] = u->fifo[0];
        memmove(u->fifo, u->fifo + 1, --u->fifo_count);
    }
    refill(u);
    pthread_mutex_unlock(&u->mutex);

    return n;
}

uint8_t UART_ReceiveByte(LPC_UART_TypeDef *UARTx)
{
    uint8_t byte = 0;
    UART_Receive(UARTx, &byte, 1, NONE_BLOCKING);
    return byte;
}

uint32_t UART_Send(LPC_UART_TypeDef *UARTx, uint8_t *txbuf, uint32_t buflen, TRANSFER_BLOCK_Type flag)
{
    (void)flag;
    const int i = uart_index(UARTx);
    if (i < 0 || ! buflen)
        return 0;

    if (g_tx)
        g_tx((uint8_t)i, txbuf, buflen);

    if (g_uarts[i].int_thre)
        emu_irq_pend(g_irqs[i]);

    return buflen;
}

void UART_SendByte(LPC_UART_TypeDef *UARTx, uint8_t Data)
{
    UART_Send(UARTx, &Data, 1, NONE_BLOCKING);
}

/* the handler of a uart runs again while it still has something to do */
void emu_uart_after_isr(int i)
{
    if (i >= 0 && i < EMU_UART_COUNT)
        update_irq(i);
}
