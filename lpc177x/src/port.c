/*
 * mod-hw-emulator — FreeRTOS port for the host
 *
 * Every task is a host thread waiting on its own run flag; only the thread of pxCurrentTCB runs. A context
 * switch is vTaskSwitchContext() followed by handing the run flag to the thread of the new pxCurrentTCB.
 *
 * Preemption happens at preemption points: kernel calls (critical section exit, yield) and emulated hardware
 * accesses. Interrupts and the tick only request the switch, the running task takes it at its next point.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "emu_core.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct emu_thread {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int run;
    int dying;
    atomic_int preempted;   /* switched away by the watchdog while running: park at the next point */
    TaskFunction_t code;
    void *parameters;
} emu_thread;

extern void * volatile pxCurrentTCB;

static _Thread_local emu_thread *t_self;      /* the task this thread runs, NULL outside tasks */
static _Thread_local int t_critical;           /* critical section nesting */
static _Thread_local int t_irq_disabled;       /* portDISABLE_INTERRUPTS() taken by this thread */
static _Thread_local int t_yield_pending;      /* yield asked inside a critical section */
static _Thread_local uint64_t t_last_yield;    /* last explicit taskYIELD(), host us */

static atomic_int g_switch_requested;
static _Atomic uint64_t g_switch_request_us;   /* when the pending switch was requested */
static atomic_int g_halted;
static atomic_int g_scheduler_started;

/* a requested switch the running task has not taken after this long is forced (see emu_port_watchdog) */
#define FORCED_SWITCH_US 2000u

static pthread_mutex_t g_stop_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_stop_cond = PTHREAD_COND_INITIALIZER;

/* host time a task that yields with nothing else to run gives back, in microseconds; a context switch
 * request ends it early */
static unsigned g_idle_yield_us = 10000;

static pthread_mutex_t g_idle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_idle_cond = PTHREAD_COND_INITIALIZER;

static void sleep_us(unsigned us)
{
    struct timespec ts = { .tv_sec = us / 1000000u, .tv_nsec = (long)(us % 1000000u) * 1000 };
    nanosleep(&ts, NULL);
}

/* sleeps up to `us`, less if a context switch is requested meanwhile */
static void idle_wait(unsigned us)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    const uint64_t ns = (uint64_t)until.tv_nsec + (uint64_t)us * 1000u;
    until.tv_sec += (time_t)(ns / 1000000000u);
    until.tv_nsec = (long)(ns % 1000000000u);

    pthread_mutex_lock(&g_idle_mutex);
    while (! atomic_load(&g_switch_requested))
        if (pthread_cond_timedwait(&g_idle_cond, &g_idle_mutex, &until) != 0)
            break;
    pthread_mutex_unlock(&g_idle_mutex);
}

static void request_switch(void)
{
    if (! atomic_exchange(&g_switch_requested, 1))
        atomic_store(&g_switch_request_us, emu_now_us());
    pthread_mutex_lock(&g_idle_mutex);
    pthread_cond_broadcast(&g_idle_cond);
    pthread_mutex_unlock(&g_idle_mutex);
}

/* the task thread is stored in the first slot of its (otherwise unused) FreeRTOS stack */
static emu_thread *thread_of(void *tcb)
{
    if (! tcb)
        return NULL;
    StackType_t *top = *(StackType_t **)tcb;
    return (emu_thread *)top[0];
}

static void thread_wake(emu_thread *t)
{
    pthread_mutex_lock(&t->mutex);
    t->run = 1;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);
}

static void thread_wait(emu_thread *t)
{
    pthread_mutex_lock(&t->mutex);
    while (! t->run && ! t->dying)
        pthread_cond_wait(&t->cond, &t->mutex);
    t->run = 0;
    const int dying = t->dying;
    pthread_mutex_unlock(&t->mutex);

    if (dying)
        pthread_exit(NULL);
}

static int is_current(const emu_thread *t)
{
    return thread_of(pxCurrentTCB) == t;
}

/*
 * Takes the irq lock as the current task.
 *
 * A task switched away by the watchdog keeps running until it enters the kernel, then parks. The check has to
 * be made holding the lock: the watchdog switches only holding it, so once the lock is ours and we are current
 * we stay current until we release it. Checked before taking the lock, a switch could slip in between and the
 * task would go on in the kernel as if it were current (placing the new current task on an event list, say),
 * which corrupts the task lists.
 *
 * Returns with the lock held once more by this thread.
 */
static _Thread_local int t_barrier;     /* inside vTaskSuspendAll(), between its two barriers */

static void lock_as_current(void)
{
    emu_irq_lock();

    emu_thread *self = t_self;
    /* outside tasks, or the lock was already ours: nobody could switch this task meanwhile */
    if (! self || emu_in_isr() || t_critical > 0 || t_irq_disabled || t_barrier)
        return;

    while (atomic_exchange(&self->preempted, 0))
    {
        if (is_current(self))
        {
            /* scheduled again before it got here: drop the wake-up it was given meanwhile */
            pthread_mutex_lock(&self->mutex);
            self->run = 0;
            pthread_mutex_unlock(&self->mutex);
            break;
        }

        emu_irq_unlock();
        thread_wait(self);
        emu_irq_lock();
    }
}

static void park_forever(void)
{
    for (;;)
        sleep_us(1000000u);
}

static void *task_thread(void *arg)
{
    emu_thread *self = arg;
    t_self = self;

    thread_wait(self);

    /* name the host thread after the task, for debuggers and profilers */
    char name[16];
    snprintf(name, sizeof(name), "fw:%s", pcTaskGetName(NULL));
#if defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__) || defined(_WIN32)
    pthread_setname_np(pthread_self(), name);
#endif

    self->code(self->parameters);

    /* a FreeRTOS task must never return */
    emu_log(MHE_LOG_ERROR, "a task returned from its function");
    vTaskDelete(NULL);
    park_forever();
    return NULL;
}

/* switches away from the current task; called with the irq lock held once, releases it */
static int switch_context(void)
{
    emu_thread *self = t_self;

    atomic_store(&g_switch_requested, 0);
    vTaskSwitchContext();

    emu_thread *next = thread_of(pxCurrentTCB);
    if (next == self)
    {
        emu_irq_unlock();
        return 0;
    }

    if (next)
        thread_wake(next);

    emu_irq_unlock();

    if (atomic_load(&g_halted))
        park_forever();

    if (self)
        thread_wait(self);

    return 1;
}

static void yield(int idle)
{
    if (emu_in_isr())
    {
        request_switch();
        return;
    }

    if (! t_self)
        return;

    if (t_critical > 0 || t_irq_disabled)
    {
        t_yield_pending = 1;
        return;
    }

    /* taskYIELD() loops (the display task, the post boot task) would spin a host core: a task gets one
     * explicit yield per g_idle_yield_us, it waits for the rest unless a context switch is requested */
    if (idle && g_idle_yield_us)
    {
        const uint64_t now = emu_now_us();
        if (now - t_last_yield < g_idle_yield_us)
            idle_wait((unsigned)(g_idle_yield_us - (now - t_last_yield)));
        t_last_yield = emu_now_us();
    }

    /* switch_context() switches away from the current task: it must be this one */
    lock_as_current();
    switch_context();
}

/* taskYIELD() */
void vPortYield(void)
{
    yield(1);
}

/* yields of the kernel itself (a task blocks, a higher priority task is ready) */
void vPortYieldWithinAPI(void)
{
    yield(0);
}

void vPortYieldFromISR(void)
{
    request_switch();
}

void emu_request_switch(void)
{
    request_switch();
}

void emu_preemption_point(void)
{
    if (atomic_load(&g_halted) && t_self && ! emu_in_isr())
        park_forever();

    if (! t_self || emu_in_isr() || t_critical != 0 || t_irq_disabled || t_barrier)
        return;

    /* switched away by the watchdog: park here */
    if (atomic_load(&t_self->preempted))
    {
        lock_as_current();
        emu_irq_unlock();
    }

    if (atomic_load(&g_switch_requested))
        yield(0);
}

/* ---------------------------------------------------------------------------------------------------------------
 * interrupt masking
 */

void vPortDisableInterrupts(void)
{
    if (t_irq_disabled)
        return;
    lock_as_current();
    t_irq_disabled = 1;
}

void vPortEnableInterrupts(void)
{
    if (! t_irq_disabled)
        return;
    t_irq_disabled = 0;
    emu_irq_unlock();

    if (t_yield_pending && t_critical == 0)
    {
        t_yield_pending = 0;
        yield(0);
    }
}

/*
 * vTaskSuspendAll() is "barrier; ++uxSchedulerSuspended; barrier". The lock is held across the increment, so the
 * watchdog sees the scheduler either still running (and switches before this task got here: it parks) or
 * suspended (and does not switch).
 */
void vPortSoftwareBarrier(void)
{
    lock_as_current();
    ++t_barrier;
}

void vPortMemoryBarrier(void)
{
    if (t_barrier == 0)
        return;
    --t_barrier;
    emu_irq_unlock();
}

void vPortEnterCritical(void)
{
    lock_as_current();
    ++t_critical;
}

void vPortExitCritical(void)
{
    if (t_critical == 0)
        return;

    --t_critical;
    emu_irq_unlock();

    if (t_critical == 0 && t_yield_pending && ! t_irq_disabled && ! emu_in_isr())
    {
        t_yield_pending = 0;
        yield(0);
    }
}

/* the FromISR calls, also made from tasks (a serial callback, say) */
UBaseType_t uxPortSetInterruptMaskFromISR(void)
{
    lock_as_current();
    return 0;
}

void vPortClearInterruptMaskFromISR(UBaseType_t mask)
{
    (void)mask;
    emu_irq_unlock();
}

/* ---------------------------------------------------------------------------------------------------------------
 * tasks
 */

StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters)
{
    emu_thread *t = calloc(1, sizeof(*t));
    if (! t)
        abort();

    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond, NULL);
    t->code = pxCode;
    t->parameters = pvParameters;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 1024u * 1024u);
    if (pthread_create(&t->thread, &attr, task_thread, t) != 0)
        abort();
    pthread_attr_destroy(&attr);

    pxTopOfStack[0] = (StackType_t)t;
    return pxTopOfStack;
}

void vPortTaskDeleting(void *tcb)
{
    emu_thread *t = thread_of(tcb);
    if (t)
        t->dying = 1;
}

void vPortCleanUpTCB(void *tcb)
{
    emu_thread *t = thread_of(tcb);
    if (! t || t == t_self)
        return;

    /* the thread of a task deleted by another task is parked: let it go */
    pthread_mutex_lock(&t->mutex);
    const int was_dying = t->dying;
    t->dying = 1;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    (void)was_dying;
}

/* ---------------------------------------------------------------------------------------------------------------
 * scheduler
 */

static void tick_isr(void)
{
    if (xTaskIncrementTick() != pdFALSE)
        request_switch();
}

BaseType_t xPortStartScheduler(void)
{
    /* vTaskStartScheduler() masks interrupts, the first task unmasks them */
    if (t_irq_disabled)
    {
        t_irq_disabled = 0;
        emu_irq_unlock();
    }

    emu_irq_set_handler(EMU_IRQ_TICK, tick_isr);
    emu_irq_set_period(EMU_IRQ_TICK, 1000000u / configTICK_RATE_HZ);
    emu_irq_enable(EMU_IRQ_TICK, 1);

    atomic_store(&g_scheduler_started, 1);

    emu_thread *first = thread_of(pxCurrentTCB);
    if (first)
        thread_wake(first);

    /* the thread that started the scheduler waits until the emulation stops */
    pthread_mutex_lock(&g_stop_mutex);
    while (! atomic_load(&g_halted))
        pthread_cond_wait(&g_stop_cond, &g_stop_mutex);
    pthread_mutex_unlock(&g_stop_mutex);

    park_forever();
    return pdFALSE;
}

void vPortEndScheduler(void)
{
    emu_port_halt();
}

void emu_port_halt(void)
{
    pthread_mutex_lock(&g_stop_mutex);
    atomic_store(&g_halted, 1);
    pthread_cond_broadcast(&g_stop_cond);
    pthread_mutex_unlock(&g_stop_mutex);
}

/*
 * Called by the interrupt thread. On the MCU an interrupt that readies a higher priority task preempts the
 * running one at once; here the running task takes the switch at its next preemption point. A task spinning
 * on a flag (`while (g_system_blocked);`) has none, so after FORCED_SWITCH_US the switch is made for it: the
 * new task runs and the old thread parks at its next point. Until then both threads run, as if the old one
 * had been interrupted right there.
 */
void emu_port_watchdog(void)
{
    if (! atomic_load(&g_scheduler_started) || ! atomic_load(&g_switch_requested) || atomic_load(&g_halted))
        return;
    if (emu_now_us() - atomic_load(&g_switch_request_us) < FORCED_SWITCH_US)
        return;

    emu_irq_lock();
    if (atomic_load(&g_switch_requested))
    {
        emu_thread *old = thread_of(pxCurrentTCB);
        atomic_store(&g_switch_requested, 0);
        vTaskSwitchContext();
        emu_thread *next = thread_of(pxCurrentTCB);
        if (next != old)
        {
            if (old)
                atomic_store(&old->preempted, 1);
            if (next)
                thread_wake(next);
        }
    }
    emu_irq_unlock();
}

void emu_port_set_idle_yield(unsigned us)
{
    g_idle_yield_us = us;
}

/* the idle task: takes pending switches and gives the host CPU back */
void vApplicationIdleHook(void)
{
    if (atomic_load(&g_switch_requested))
        yield(0);
    else
        idle_wait(1000);
}

/* ---------------------------------------------------------------------------------------------------------------
 * heap: the host allocator
 */

void *pvPortMalloc(size_t xWantedSize)
{
    void *p = malloc(xWantedSize ? xWantedSize : 1);
#if (configUSE_MALLOC_FAILED_HOOK == 1)
    if (! p)
    {
        extern void vApplicationMallocFailedHook(void);
        vApplicationMallocFailedHook();
    }
#endif
    return p;
}

void vPortFree(void *pv)
{
    free(pv);
}

void vPortDefineHeapRegions(const HeapRegion_t * const pxHeapRegions)
{
    (void)pxHeapRegions;
}

size_t xPortGetFreeHeapSize(void)
{
    return 64u * 1024u;
}

size_t xPortGetMinimumEverFreeHeapSize(void)
{
    return 64u * 1024u;
}

void vPortInitialiseBlocks(void)
{
}
