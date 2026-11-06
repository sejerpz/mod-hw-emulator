/*
 * mod-hw-emulator — emulated mod-system-control
 *
 * Controller side: a command is "sys_xxx LEN" for a query or "sys_xxx LEN ARGS" to set something, LEN being
 * the size of ARGS in hexadecimal. Queries are answered "r 0 VALUE", settings "r 0". Values are kept by
 * command and arguments and saved to a file.
 *
 * mod-host side (MHE_WITH_SYS_HOST): mod-system-control's sys_host.c is built in as it is; the functions it
 * takes from the rest of mod-system-control (serial line, replies, files) are implemented here.
 */

#include "syscontrol.h"

#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_MSG    256
#define MAX_QUEUE  512
#define MAX_VALUES 128

typedef struct {
    char key[48];
    char value[128];
} entry;

struct mhe_syscontrol {
    mhe_syscontrol_reply reply;
    void *ctx;
    char state_dir[1024];
    char state_file[1100];
    int debug;
    int has_host;

    /* values the controller set */
    pthread_mutex_t values_mutex;
    entry values[MAX_VALUES];
    int n_values;

    /* bytes of a message being received */
    char msg[MAX_MSG];
    size_t msg_len;

    /* whole messages from the controller, for the system-control thread */
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    char queue[MAX_QUEUE][MAX_MSG];
    int queue_head, queue_count;

    pthread_t thread;
    atomic_int running;
};

/* the one instance, for the functions sys_host.c calls */
static mhe_syscontrol *g_sc;

/* what a new device answers */
static const entry g_defaults[] = {
    { "sys_ver",           "v1.13-emu" },
    { "sys_ser",           "MDW01EMULATOR" },
    { "sys_bts",           "Disabled" },
    { "sys_usb",           "0" },
    { "sys_mnr",           "0" },
};

/* ---------------------------------------------------------------------------------------------------------------
 * values
 */

static entry *find(mhe_syscontrol *sc, const char *key)
{
    for (int i = 0; i < sc->n_values; ++i)
        if (! strcmp(sc->values[i].key, key))
            return &sc->values[i];
    return NULL;
}

static void put(mhe_syscontrol *sc, const char *key, const char *value)
{
    entry *e = find(sc, key);
    if (! e && sc->n_values < MAX_VALUES)
    {
        e = &sc->values[sc->n_values++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    if (e)
        snprintf(e->value, sizeof(e->value), "%s", value);
}

static void save(mhe_syscontrol *sc)
{
    FILE *f = fopen(sc->state_file, "w");
    if (! f)
        return;
    for (int i = 0; i < sc->n_values; ++i)
        fprintf(f, "%s=%s\n", sc->values[i].key, sc->values[i].value);
    fclose(f);
}

static void load(mhe_syscontrol *sc)
{
    for (size_t i = 0; i < sizeof(g_defaults) / sizeof(g_defaults[0]); ++i)
        put(sc, g_defaults[i].key, g_defaults[i].value);

    FILE *f = fopen(sc->state_file, "r");
    if (! f)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (! eq)
            continue;
        *eq = 0;
        put(sc, line, eq + 1);
    }
    fclose(f);
}

static void send_reply(mhe_syscontrol *sc, const char *text)
{
    sc->reply(sc->ctx, (const uint8_t *)text, strlen(text) + 1u);
}

/* ---------------------------------------------------------------------------------------------------------------
 * the controller's system commands
 */

/*
 * Arguments of the setting form of each command (mod-protocol.h), the value being the last one: with one
 * argument less the command is a query of that value. "sys_gio 03 0 2" asks the gain of input 0 channel 2,
 * "sys_gio 08 0 2 -3.5" sets it. Commands with no arguments are actions or plain queries.
 */
static const struct { const char *cmd; int args; } g_arity[] = {
    { "sys_gio", 3 }, { "sys_ghp", 1 }, { "sys_cvi", 1 }, { "sys_exp", 1 }, { "sys_cvo", 1 },
    { "sys_ngc", 1 }, { "sys_ngt", 1 }, { "sys_ngd", 1 }, { "sys_cmm", 1 }, { "sys_cmr", 1 },
    { "sys_pbg", 1 }, { "sys_usb", 1 }, { "sys_mnr", 1 },
    { "sys_ver", 2 }, { "sys_ctl", 2 },
    { "sys_ams", 0 }, { "sys_bts", 0 }, { "sys_btd", 0 }, { "sys_ser", 0 }, { "sys_rbt", 0 },
};

static int arity(const char *cmd)
{
    for (size_t i = 0; i < sizeof(g_arity) / sizeof(g_arity[0]); ++i)
        if (! strcmp(g_arity[i].cmd, cmd))
            return g_arity[i].args;
    return -1;
}

#ifdef MHE_WITH_SYS_HOST
#include "sys_host.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

/* answers a "sys_*" command of the controller */
static void handle(mhe_syscontrol *sc, const char *message)
{
    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg), "%s", message);

    char *cmd = strtok(msg, " ");
    if (! cmd || strncmp(cmd, "sys_", 4) != 0)
    {
        send_reply(sc, "r -1");
        return;
    }

    strtok(NULL, " "); /* LEN */

    char *argv[8];
    int argc = 0;
    char *tok;
    while (argc < 8 && (tok = strtok(NULL, " ")))
        argv[argc++] = tok;

    /* the page in view: what mod-host widgets and labels are sent for */
    if (! strcmp(cmd, "sys_pch") || ! strcmp(cmd, "sys_spc"))
    {
#ifdef MHE_WITH_SYS_HOST
        if (sc->has_host && argc > 0)
        {
            if (cmd[5] == 'c')
                sys_host_set_hmi_page(atoi(argv[0]));
            else
                sys_host_set_hmi_subpage(atoi(argv[0]));
        }
#endif
        send_reply(sc, argc > 0 ? "r 0" : "r -1");
        return;
    }

    const int n = arity(cmd);
    char key[128], reply[256];
    int len = snprintf(key, sizeof(key), "%s", cmd);

    pthread_mutex_lock(&sc->values_mutex);

    if (n > 0 && argc >= n)
    {
        /* setting: the key is the command and all arguments but the value */
        for (int i = 0; i < n - 1; ++i)
            len += snprintf(key + len, sizeof(key) - (size_t)len, " %s", argv[i]);
        put(sc, key, argv[n - 1]);
        save(sc);
        pthread_mutex_unlock(&sc->values_mutex);
        send_reply(sc, "r 0");
        return;
    }

    /* query (or unknown command): answer the value kept for it, 0 if none */
    for (int i = 0; i < argc; ++i)
        len += snprintf(key + len, sizeof(key) - (size_t)len, " %s", argv[i]);
    const entry *e = find(sc, key);
    if (! e && argc)
        e = find(sc, cmd);
    snprintf(reply, sizeof(reply), "r 0 %s", e ? e->value : "0");
    pthread_mutex_unlock(&sc->values_mutex);
    send_reply(sc, reply);
}

/* ---------------------------------------------------------------------------------------------------------------
 * messages from the controller
 */

static void queue_push(mhe_syscontrol *sc, const char *msg)
{
    pthread_mutex_lock(&sc->queue_mutex);
    if (sc->queue_count < MAX_QUEUE)
    {
        const int at = (sc->queue_head + sc->queue_count) % MAX_QUEUE;
        snprintf(sc->queue[at], MAX_MSG, "%s", msg);
        sc->queue_count++;
        pthread_cond_signal(&sc->queue_cond);
    }
    pthread_mutex_unlock(&sc->queue_mutex);
}

/* the next message, waiting up to timeout_ms; length, or -1 if none */
static int queue_pop(mhe_syscontrol *sc, char *buf, size_t size, int timeout_ms)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    const long long ns = until.tv_nsec + (long long)timeout_ms * 1000000LL;
    until.tv_sec += (time_t)(ns / 1000000000LL);
    until.tv_nsec = (long)(ns % 1000000000LL);

    pthread_mutex_lock(&sc->queue_mutex);
    while (! sc->queue_count && atomic_load(&sc->running))
        if (pthread_cond_timedwait(&sc->queue_cond, &sc->queue_mutex, &until) != 0)
            break;

    int len = -1;
    if (sc->queue_count)
    {
        snprintf(buf, size, "%s", sc->queue[sc->queue_head]);
        sc->queue_head = (sc->queue_head + 1) % MAX_QUEUE;
        sc->queue_count--;
        len = (int)strlen(buf);
    }
    pthread_mutex_unlock(&sc->queue_mutex);
    return len;
}

void mhe_syscontrol_feed(mhe_syscontrol *sc, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        if (data[i] == 0)
        {
            sc->msg[sc->msg_len] = 0;
            if (sc->msg_len)
                queue_push(sc, sc->msg);
            sc->msg_len = 0;
        }
        else if (sc->msg_len < MAX_MSG - 1)
        {
            sc->msg[sc->msg_len++] = (char)data[i];
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * what sys_host.c takes from the rest of mod-system-control
 */

#ifdef MHE_WITH_SYS_HOST

#include "cli.h"
#include "reply.h"
#include "serial_rw.h"

int serial_read_message(struct sp_port *serialport, char buf[0xff], int timeout_ms)
{
    (void)serialport;
    const int len = queue_pop(g_sc, buf, 0xff, timeout_ms);
    return len < 0 ? SP_READ_ERROR_NO_DATA : len;
}

bool write_or_close(struct sp_port *serialport, const char *msg)
{
    (void)serialport;
    send_reply(g_sc, msg);
    return true;
}

bool parse_and_reply_to_message(struct sp_port *serialport, char msg[0xff], bool debug)
{
    (void)serialport; (void)debug;
    handle(g_sc, msg);
    return true;
}

/* files of the device (/data/audioproc.txt) live in the state folder */
static void state_path(char *out, size_t size, const char *filename)
{
    const char *base = strrchr(filename, '/');
    snprintf(out, size, "%s/%s", g_sc->state_dir, base ? base + 1 : filename);
}

bool read_file(char buf[0xff], const char *filename, bool debug)
{
    (void)debug;
    char path[1200];
    state_path(path, sizeof(path), filename);
    FILE *f = fopen(path, "r");
    if (! f)
        return false;
    const size_t n = fread(buf, 1, 0xff - 1, f);
    buf[n] = 0;
    fclose(f);
    return n > 0;
}

bool write_file(char buf[0xff], const char *filename, bool debug)
{
    (void)debug;
    char path[1200];
    state_path(path, sizeof(path), filename);
    FILE *f = fopen(path, "w");
    if (! f)
        return false;
    fputs(buf, f);
    fclose(f);
    return true;
}

#endif

/* ---------------------------------------------------------------------------------------------------------------
 * the system-control thread: mod-system-control's main loop
 */

static void *system_control_thread(void *arg)
{
    mhe_syscontrol *sc = arg;
    char buf[MAX_MSG];

    while (atomic_load(&sc->running))
    {
        const int len = queue_pop(sc, buf, sizeof(buf), 40);

        if (len < 0)
        {
#ifdef MHE_WITH_SYS_HOST
            /* forwards what mod-host sent: labels, leds, widget bitmaps */
            if (sc->has_host)
                sys_host_process(NULL);
#endif
            continue;
        }

        /* a response that came after its wait was over */
        if (buf[0] == 'r' && buf[1] == ' ')
            continue;

        handle(sc, buf);
    }
    return NULL;
}

mhe_syscontrol *mhe_syscontrol_create(mhe_syscontrol_reply reply, void *ctx, const char *state_dir, int debug)
{
    if (g_sc)
        return NULL; /* mod-system-control is one per system */

    mhe_syscontrol *sc = calloc(1, sizeof(*sc));
    if (! sc)
        return NULL;
    sc->reply = reply;
    sc->ctx = ctx;
    sc->debug = debug;
    snprintf(sc->state_dir, sizeof(sc->state_dir), "%s", state_dir ? state_dir : ".");
    snprintf(sc->state_file, sizeof(sc->state_file), "%s/system-control.txt", sc->state_dir);
    pthread_mutex_init(&sc->values_mutex, NULL);
    pthread_mutex_init(&sc->queue_mutex, NULL);
    pthread_cond_init(&sc->queue_cond, NULL);
    load(sc);
    g_sc = sc;

#ifdef MHE_WITH_SYS_HOST
    /* creates /sys_msgs: mod-host must start after it. MHE_NO_SYS_HOST=1 leaves it alone (a second emulator
     * running next to one mod-host is talking to, a test) */
    if (! getenv("MHE_NO_SYS_HOST"))
    {
        sys_host_setup(debug != 0);
        const int fd = shm_open("/sys_msgs", O_RDWR, 0);
        sc->has_host = fd >= 0;
        if (fd >= 0)
            close(fd);
    }
#endif

    atomic_store(&sc->running, 1);
    if (pthread_create(&sc->thread, NULL, system_control_thread, sc) != 0)
    {
        atomic_store(&sc->running, 0);
        free(sc);
        g_sc = NULL;
        return NULL;
    }
    return sc;
}

void mhe_syscontrol_destroy(mhe_syscontrol *sc)
{
    if (! sc)
        return;
    atomic_store(&sc->running, 0);
    pthread_mutex_lock(&sc->queue_mutex);
    pthread_cond_broadcast(&sc->queue_cond);
    pthread_mutex_unlock(&sc->queue_mutex);
    pthread_join(sc->thread, NULL);
#ifdef MHE_WITH_SYS_HOST
    if (sc->has_host)
        sys_host_destroy();
#endif
    g_sc = NULL;
    free(sc);
}

int mhe_syscontrol_has_host(const mhe_syscontrol *sc)
{
    return sc && sc->has_host;
}
