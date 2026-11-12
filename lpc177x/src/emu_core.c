/*
 * mod-hw-emulator — virtual time, irq lock and interrupt controller thread
 */

#include "emu_core.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

const mhe_host *g_emu_host;

void emu_log(mhe_log_level level, const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (g_emu_host && g_emu_host->log)
        g_emu_host->log(g_emu_host->handle, level, buffer);
    else
        fprintf(stderr, "[fw] %s\n", buffer);
}

/* ---------------------------------------------------------------------------------------------------------------
 * virtual time
 */

static uint64_t g_epoch_ns;
static _Thread_local int t_in_isr;
static _Thread_local uint64_t t_isr_now;

static uint64_t host_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t host_us(void)
{
    if (! g_epoch_ns)
        g_epoch_ns = host_ns();
    return (host_ns() - g_epoch_ns) / 1000u;
}

uint64_t emu_now_us(void)
{
    return t_in_isr ? t_isr_now : host_us();
}

int emu_in_isr(void)
{
    return t_in_isr;
}

/* ---------------------------------------------------------------------------------------------------------------
 * irq lock
 */

static pthread_mutex_t g_irq_mutex;

void emu_irq_lock(void)
{
    pthread_mutex_lock(&g_irq_mutex);
}

void emu_irq_unlock(void)
{
    pthread_mutex_unlock(&g_irq_mutex);
}

/* ---------------------------------------------------------------------------------------------------------------
 * interrupt controller
 */

typedef struct {
    emu_isr_fn handler;
    atomic_int enabled;
    atomic_int pending;
    uint64_t period_us;
    uint64_t next_due;
} irq_source;

static irq_source g_irqs[EMU_IRQ_MAX];
static pthread_mutex_t g_ctl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ctl_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_irq_thread;
static atomic_int g_running;
static atomic_int g_kick;

/* a timer that falls this much behind skips ahead instead of catching up */
#define MAX_LAG_US 200000u

/* shortest sleep of the interrupt thread: periodic sources due meanwhile run in a batch */
#define MIN_SLEEP_US 1000u

void emu_irq_set_handler(int irq, emu_isr_fn fn)
{
    if (irq >= 0 && irq < EMU_IRQ_MAX)
        g_irqs[irq].handler = fn;
}

void emu_irq_enable(int irq, int enable)
{
    if (irq < 0 || irq >= EMU_IRQ_MAX)
        return;

    pthread_mutex_lock(&g_ctl_mutex);
    if (enable && ! g_irqs[irq].enabled && g_irqs[irq].period_us)
        g_irqs[irq].next_due = host_us() + g_irqs[irq].period_us;
    g_irqs[irq].enabled = enable;
    atomic_store(&g_kick, 1);
    pthread_cond_signal(&g_ctl_cond);
    pthread_mutex_unlock(&g_ctl_mutex);
}

void emu_irq_set_period(int irq, uint64_t period_us)
{
    if (irq < 0 || irq >= EMU_IRQ_MAX)
        return;

    pthread_mutex_lock(&g_ctl_mutex);
    g_irqs[irq].period_us = period_us;
    g_irqs[irq].next_due = host_us() + period_us;
    atomic_store(&g_kick, 1);
    pthread_cond_signal(&g_ctl_cond);
    pthread_mutex_unlock(&g_ctl_mutex);
}

void emu_irq_pend(int irq)
{
    if (irq < 0 || irq >= EMU_IRQ_MAX)
        return;

    atomic_store(&g_irqs[irq].pending, 1);

    pthread_mutex_lock(&g_ctl_mutex);
    atomic_store(&g_kick, 1);
    pthread_cond_signal(&g_ctl_cond);
    pthread_mutex_unlock(&g_ctl_mutex);
}

static void run_handler(irq_source *src, uint64_t at)
{
    if (! src->handler)
        return;

    emu_irq_lock();
    t_in_isr = 1;
    t_isr_now = at;
    src->handler();
    t_in_isr = 0;
    emu_irq_unlock();
}

static void *irq_thread(void *arg)
{
    (void)arg;

    while (atomic_load(&g_running))
    {
        const uint64_t now = host_us();
        uint64_t earliest = now + 5000u;

        /* periodic sources, in due order */
        for (;;)
        {
            irq_source *due = NULL;

            for (int i = 0; i < EMU_IRQ_MAX; ++i)
            {
                irq_source *src = &g_irqs[i];
                if (! src->enabled || ! src->period_us || src->next_due > now)
                    continue;
                if (! due || src->next_due < due->next_due)
                    due = src;
            }

            if (! due)
                break;

            const uint64_t at = due->next_due;
            due->next_due += due->period_us;
            if (now - at > MAX_LAG_US)
                due->next_due = now + due->period_us;

            run_handler(due, at);
        }

        /* asynchronous requests (in irq number order, as the NVIC with equal priorities) */
        for (int i = 0; i < EMU_IRQ_MAX; ++i)
        {
            irq_source *src = &g_irqs[i];
            if (src->enabled && atomic_exchange(&src->pending, 0))
                run_handler(src, host_us());
        }

        emu_port_watchdog();

        int pending = 0;
        for (int i = 0; i < EMU_IRQ_MAX; ++i)
        {
            const irq_source *src = &g_irqs[i];
            if (src->enabled && src->period_us && src->next_due < earliest)
                earliest = src->next_due;
            if (src->enabled && atomic_load(&src->pending))
                pending = 1;
        }

        if (pending)
            continue;

        /* sleep until the next due source or a request; fast timers (the ~30 kHz led timer) run in batches */
        pthread_mutex_lock(&g_ctl_mutex);
        if (! atomic_exchange(&g_kick, 0) && atomic_load(&g_running))
        {
            const uint64_t t = host_us();
            uint64_t wait_us = earliest > t ? earliest - t : 0;
            if (wait_us < MIN_SLEEP_US)
                wait_us = MIN_SLEEP_US;
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            uint64_t ns = (uint64_t)until.tv_nsec + (wait_us > 1000000u ? 1000000u : wait_us) * 1000u;
            until.tv_sec += (time_t)(ns / 1000000000u);
            until.tv_nsec = (long)(ns % 1000000000u);
            pthread_cond_timedwait(&g_ctl_cond, &g_ctl_mutex, &until);
        }
        pthread_mutex_unlock(&g_ctl_mutex);
    }

    return NULL;
}

static void init_irq_mutex(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_irq_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

static pthread_once_t g_once = PTHREAD_ONCE_INIT;

__attribute__((constructor)) static void emu_core_init(void)
{
    pthread_once(&g_once, init_irq_mutex);
    host_us();
}

int emu_core_start(void)
{
    pthread_once(&g_once, init_irq_mutex);

    atomic_store(&g_running, 1);
    if (pthread_create(&g_irq_thread, NULL, irq_thread, NULL) != 0)
    {
        atomic_store(&g_running, 0);
        return -1;
    }
    return 0;
}

void emu_core_stop(void)
{
    if (! atomic_exchange(&g_running, 0))
        return;

    pthread_mutex_lock(&g_ctl_mutex);
    pthread_cond_signal(&g_ctl_cond);
    pthread_mutex_unlock(&g_ctl_mutex);

    /* not joined: a handler may wait for a task stuck in a critical section, and stopping must not hang
     * on a firmware that hangs; the thread ends at its next loop */
    pthread_detach(g_irq_thread);
}

/* ---------------------------------------------------------------------------------------------------------------
 * CMSIS NVIC
 */

#include "device.h"

void NVIC_EnableIRQ(IRQn_Type IRQn)            { emu_irq_enable((int)IRQn, 1); }
void NVIC_DisableIRQ(IRQn_Type IRQn)           { emu_irq_enable((int)IRQn, 0); }
void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority) { (void)IRQn; (void)priority; }
uint32_t NVIC_GetPriority(IRQn_Type IRQn)      { (void)IRQn; return 0; }
void NVIC_SetPendingIRQ(IRQn_Type IRQn)        { emu_irq_pend((int)IRQn); }
void NVIC_ClearPendingIRQ(IRQn_Type IRQn)      { if ((int)IRQn >= 0) atomic_store(&g_irqs[IRQn].pending, 0); }
void NVIC_SystemReset(void)                    { emu_log(MHE_LOG_WARNING, "system reset requested (ignored)"); }

void __enable_irq(void)  { vPortEnableInterrupts(); }
void __disable_irq(void) { vPortDisableInterrupts(); }
