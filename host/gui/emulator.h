/*
 * mod-hw-emulator — the emulated device for a user interface: module, system-control, console, tcp lines
 */

#pragma once

#include "mhe/emu_module.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

struct mhe_syscontrol;
struct mhe_console;
struct mhe_tcp_serial;

class Emulator
{
public:
    struct Listener {
        virtual ~Listener() = default;
        /* called on emulator threads */
        virtual void displayFrame(uint8_t display, uint16_t width, uint16_t height, std::vector<uint8_t> bits,
                                  uint8_t brightness) = 0;
        virtual void ledColor(uint8_t led, uint8_t r, uint8_t g, uint8_t b) = 0;
        virtual void log(mhe_log_level level, const std::string &message) = 0;
    };

    Emulator(Listener &listener, std::string stateDir);
    ~Emulator();

    bool load(const std::string &modulePath, std::string &error);
    /* CHANNEL=PORT, before start() */
    bool exposeTcp(const std::string &spec, std::string &error);
    bool start();
    void stop();

    const mhe_module *module() const { return m_module; }

    void turn(uint8_t encoder, int steps);
    void press(mhe_input_kind kind, uint8_t id, bool pressed);
    void pot(uint8_t id, uint16_t value);

private:
    static void hostLog(void *h, mhe_log_level level, const char *message);
    static void hostDisplay(void *h, uint8_t display, uint16_t w, uint16_t hgt, mhe_pixel_format format,
                            const uint8_t *bits, uint8_t brightness);
    static void hostLed(void *h, uint8_t led, uint8_t r, uint8_t g, uint8_t b);
    static void hostSerial(void *h, uint8_t channel, const uint8_t *data, size_t size);
    static void sysReply(void *ctx, const uint8_t *data, size_t size);
    static void consoleReply(void *ctx, const uint8_t *data, size_t size);
    static void tcpRx(void *ctx, const uint8_t *data, size_t size);

    Listener &m_listener;
    std::string m_stateDir;
    mhe_host m_host {};
    const mhe_module *m_module = nullptr;
    mhe_syscontrol *m_sys = nullptr;
    mhe_console *m_console = nullptr;
    int m_systemChannel = -1;
    int m_cliChannel = -1;

    struct TcpLine {
        Emulator *emulator;
        uint8_t channel;
        mhe_tcp_serial *serial;
    };
    std::map<uint8_t, TcpLine *> m_tcp;
};
