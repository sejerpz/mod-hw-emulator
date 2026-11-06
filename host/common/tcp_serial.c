/*
 * mod-hw-emulator — a serial line of the controller exposed on a TCP port
 */

#include "tcp_serial.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
#endif

struct mhe_tcp_serial {
    sock_t listener;
    _Atomic sock_t client;
    pthread_t thread;
    pthread_mutex_t send_mutex;
    atomic_int running;
    mhe_tcp_rx rx;
    void *ctx;
    uint16_t port;
};

static void *bridge_thread(void *arg)
{
    mhe_tcp_serial *ts = arg;
    uint8_t buffer[4096];

    while (atomic_load(&ts->running))
    {
        sock_t client = accept(ts->listener, NULL, NULL);
        if (client == BAD_SOCK)
            continue;

        int one = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
        atomic_store(&ts->client, client);
        fprintf(stderr, "[tcp:%u] client connected\n", ts->port);

        for (;;)
        {
            const int n = (int)recv(client, (char *)buffer, sizeof(buffer), 0);
            if (n <= 0)
                break;
            ts->rx(ts->ctx, buffer, (size_t)n);
        }

        pthread_mutex_lock(&ts->send_mutex);
        atomic_store(&ts->client, BAD_SOCK);
        close_sock(client);
        pthread_mutex_unlock(&ts->send_mutex);
        fprintf(stderr, "[tcp:%u] client disconnected\n", ts->port);
    }
    return NULL;
}

mhe_tcp_serial *mhe_tcp_serial_open(uint16_t port, int any, mhe_tcp_rx rx, void *ctx)
{
#ifdef _WIN32
    static int wsa_started;
    if (! wsa_started)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return NULL;
        wsa_started = 1;
    }
#endif

    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK)
        return NULL;

    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(any ? INADDR_ANY : INADDR_LOOPBACK);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s, 1) != 0)
    {
        fprintf(stderr, "[tcp:%u] cannot listen\n", port);
        close_sock(s);
        return NULL;
    }

    mhe_tcp_serial *ts = calloc(1, sizeof(*ts));
    if (! ts)
    {
        close_sock(s);
        return NULL;
    }
    ts->listener = s;
    ts->client = BAD_SOCK;
    ts->rx = rx;
    ts->ctx = ctx;
    ts->port = port;
    pthread_mutex_init(&ts->send_mutex, NULL);
    atomic_store(&ts->running, 1);

    if (pthread_create(&ts->thread, NULL, bridge_thread, ts) != 0)
    {
        close_sock(s);
        free(ts);
        return NULL;
    }
    pthread_detach(ts->thread);
    return ts;
}

void mhe_tcp_serial_close(mhe_tcp_serial *ts)
{
    if (! ts)
        return;
    atomic_store(&ts->running, 0);
    close_sock(ts->listener);
    pthread_mutex_lock(&ts->send_mutex);
    const sock_t client = atomic_load(&ts->client);
    if (client != BAD_SOCK)
        close_sock(client);
    pthread_mutex_unlock(&ts->send_mutex);
    /* the thread may still be in accept/recv: the struct is left to the process exit */
}

void mhe_tcp_serial_send(mhe_tcp_serial *ts, const uint8_t *data, size_t size)
{
    if (! ts)
        return;
    pthread_mutex_lock(&ts->send_mutex);
    const sock_t client = atomic_load(&ts->client);
    if (client != BAD_SOCK)
    {
        size_t sent = 0;
        while (sent < size)
        {
            const int n = (int)send(client, (const char *)data + sent, (int)(size - sent), 0);
            if (n <= 0)
                break;
            sent += (size_t)n;
        }
    }
    pthread_mutex_unlock(&ts->send_mutex);
}

int mhe_tcp_serial_connected(const mhe_tcp_serial *ts)
{
    return ts && atomic_load(&ts->client) != BAD_SOCK;
}
