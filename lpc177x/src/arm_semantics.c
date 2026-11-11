/*
 * mod-hw-emulator — Cortex-M behaviours the host CPU does not share
 *
 * Integer division by zero: on a Cortex-M3 (DIV_0_TRP clear, as the firmware leaves it) SDIV/UDIV return 0,
 * and `a % 0` computed as a - (a / 0) * 0 is `a`. An x86-64 CPU traps instead. A trap raised inside the
 * firmware module is completed the ARM way: quotient 0, remainder the dividend, execution goes on after the
 * instruction. The first one of each place is logged: it is a latent firmware bug.
 *
 * On arm64 hosts (Apple Silicon, Linux arm64) division by zero already returns 0: nothing to do.
 */

#include "emu_core.h"

#include <stdint.h>
#include <string.h>

#ifndef MHE_FW_DESCRIBE
#define MHE_FW_DESCRIBE "?"   /* git describe of the firmware sources, from the Makefile */
#endif

#if defined(__x86_64__) || defined(_M_X64)

/* length of the DIV/IDIV instruction at `ip` and its operand size in bytes, 0 if it is not one */
static int decode_div(const uint8_t *ip, int *operand_size)
{
    const uint8_t *p = ip;
    int opsize16 = 0, rex_w = 0;

    for (;; ++p)
    {
        const uint8_t b = *p;
        if (b == 0x66) { opsize16 = 1; continue; }
        if (b == 0x67 || b == 0xf2 || b == 0xf3 || b == 0xf0 || b == 0x2e || b == 0x36 || b == 0x3e ||
            b == 0x26 || b == 0x64 || b == 0x65)
            continue;
        break;
    }
    if ((*p & 0xf0) == 0x40)
    {
        rex_w = (*p & 0x08) != 0;
        ++p;
    }

    const uint8_t opcode = *p++;
    if (opcode != 0xf6 && opcode != 0xf7)
        return 0;

    const uint8_t modrm = *p++;
    const int mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
    if (reg != 6 && reg != 7)
        return 0;

    if (mod != 3)
    {
        if (rm == 4)
        {
            const uint8_t sib = *p++;
            if ((sib & 7) == 5 && mod == 0)
                p += 4;
        }
        if (mod == 1)
            p += 1;
        else if (mod == 2)
            p += 4;
        else if (mod == 0 && rm == 5)
            p += 4;
    }

    *operand_size = opcode == 0xf6 ? 1 : rex_w ? 8 : opsize16 ? 2 : 4;
    return (int)(p - ip);
}

static void complete_div(uint64_t *rip, uint64_t *rax, uint64_t *rdx)
{
    int size = 0;
    const int len = decode_div((const uint8_t *)(uintptr_t)*rip, &size);

    switch (size)
    {
    case 1: /* AX / r8: AL quotient, AH remainder */
        *rax = (*rax & ~0xffffull) | ((*rax & 0xffull) << 8);
        break;
    case 2:
        *rdx = (*rdx & ~0xffffull) | (*rax & 0xffffull);
        *rax &= ~0xffffull;
        break;
    case 4:
        *rdx = *rax & 0xffffffffull;
        *rax = 0;
        break;
    default:
        *rdx = *rax;
        *rax = 0;
        break;
    }
    *rip += (uint64_t)len;
}

#define MAX_REPORTED 32
static uintptr_t g_reported[MAX_REPORTED];

/* `offset` is the address in the module: addr2line -f -e build/fw-<device>.so OFFSET gives the line */
static void report(uintptr_t ip, uintptr_t offset)
{
    for (int i = 0; i < MAX_REPORTED; ++i)
    {
        if (g_reported[i] == ip)
            return;
        if (! g_reported[i])
        {
            g_reported[i] = ip;
            break;
        }
    }
    emu_log(MHE_LOG_WARNING, "firmware divided by zero at module offset 0x%lx, the result is 0 as on the "
            "Cortex-M3 (addr2line -f -e <module> 0x%lx)", (unsigned long)offset, (unsigned long)offset);
}

#endif

#if (defined(__linux__) || defined(__APPLE__)) && defined(__x86_64__)

#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#if defined(__APPLE__)
/* <ucontext.h> wants _XOPEN_SOURCE there; the type and its machine context are in <sys/ucontext.h> */
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#include <unistd.h>

static struct sigaction g_previous;
static void *g_module_base;

static int in_module(uintptr_t ip)
{
    Dl_info info;
    return dladdr((void *)ip, &info) && info.dli_fbase == g_module_base;
}

static void on_sigfpe(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
#if defined(__linux__)
    uint64_t *rip = (uint64_t *)&uc->uc_mcontext.gregs[REG_RIP];
    uint64_t *rax = (uint64_t *)&uc->uc_mcontext.gregs[REG_RAX];
    uint64_t *rdx = (uint64_t *)&uc->uc_mcontext.gregs[REG_RDX];
#else
    uint64_t *rip = &uc->uc_mcontext->__ss.__rip;
    uint64_t *rax = &uc->uc_mcontext->__ss.__rax;
    uint64_t *rdx = &uc->uc_mcontext->__ss.__rdx;
#endif

    if (info->si_code == FPE_INTDIV && in_module((uintptr_t)*rip))
    {
        int size;
        if (decode_div((const uint8_t *)(uintptr_t)*rip, &size))
        {
            report((uintptr_t)*rip, (uintptr_t)*rip - (uintptr_t)g_module_base);
            complete_div(rip, rax, rdx);
            return;
        }
    }

    /* not ours: what would have happened without us */
    if (g_previous.sa_flags & SA_SIGINFO)
        g_previous.sa_sigaction(sig, info, context);
    else if (g_previous.sa_handler != SIG_IGN && g_previous.sa_handler != SIG_DFL)
        g_previous.sa_handler(sig);
    else
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

/*
 * An invalid memory access in the firmware. On the LPC1778 reading near address 0 reads the flash (vector
 * table) and goes unnoticed, so the device may run with a bug the emulator stops at: say where, then crash
 * as usual (the core dump or the debugger has the rest).
 */
static struct sigaction g_previous_segv, g_previous_bus;

static void on_fault(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
#if defined(__linux__)
    const uintptr_t ip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
    const uintptr_t ip = (uintptr_t)uc->uc_mcontext->__ss.__rip;
#endif

    if (in_module(ip))
    {
        char text[384];
        const unsigned long offset = (unsigned long)(ip - (uintptr_t)g_module_base);
        const int n = snprintf(text, sizeof(text),
            "[fw] firmware %s crashed: invalid memory access to %p at module offset 0x%lx "
            "(addr2line -f -e <module> 0x%lx)%s; known firmware bugs: DESIGN.md\n", MHE_FW_DESCRIBE,
            info->si_addr, offset, offset,
            (uintptr_t)info->si_addr < 0x10000 ? "; a NULL pointer, readable flash on the LPC1778" : "");
        if (n > 0)
            (void)! write(2, text, (size_t)n);
    }

    sigaction(sig, sig == SIGSEGV ? &g_previous_segv : &g_previous_bus, NULL);
    raise(sig);
}

void emu_arm_semantics_install(void)
{
    Dl_info info;
    if (dladdr((void *)emu_arm_semantics_install, &info))
        g_module_base = info.dli_fbase;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_sigfpe;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGFPE, &sa, &g_previous);

    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, &g_previous_segv);
    sigaction(SIGBUS, &sa, &g_previous_bus);
}

#elif defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))

#include <windows.h>

static HMODULE g_module;

static LONG CALLBACK on_exception(PEXCEPTION_POINTERS e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;

    HMODULE where = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)e->ContextRecord->Rip, &where);
    int size;
    if (where != g_module || ! decode_div((const uint8_t *)(uintptr_t)e->ContextRecord->Rip, &size))
        return EXCEPTION_CONTINUE_SEARCH;

    report((uintptr_t)e->ContextRecord->Rip, (uintptr_t)e->ContextRecord->Rip - (uintptr_t)g_module);
    complete_div((uint64_t *)&e->ContextRecord->Rip, (uint64_t *)&e->ContextRecord->Rax,
                 (uint64_t *)&e->ContextRecord->Rdx);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void emu_arm_semantics_install(void)
{
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)emu_arm_semantics_install, &g_module);
    AddVectoredExceptionHandler(1, on_exception);
}

#else

/* arm64 hosts divide by zero as the Cortex-M does */
void emu_arm_semantics_install(void)
{
}

#endif
