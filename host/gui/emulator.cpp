/*
 * mod-hw-emulator — the emulated device for a user interface
 */

#include "emulator.h"

extern "C" {
#include "../common/console.h"
#include "../common/module_loader.h"
#include "../common/syscontrol.h"
#include "../common/tcp_serial.h"
}

#include <cstdio>
#include <cstring>

Emulator::Emulator(Listener &listener, std::string stateDir)
    : m_listener(listener), m_stateDir(std::move(stateDir))
{
    mhe_mkdir_p(m_stateDir.c_str());
    m_host.size = sizeof(mhe_host);
    m_host.handle = this;
    m_host.log = hostLog;
    m_host.display_frame = hostDisplay;
    m_host.led_color = hostLed;
    m_host.serial_tx = hostSerial;
    m_host.state_dir = m_stateDir.c_str();
}

Emulator::~Emulator()
{
    stop();
}

bool Emulator::load(const std::string &modulePath, std::string &error)
{
    char message[512] = {};
    m_module = mhe_load_module(modulePath.c_str(), &m_host, message, sizeof(message));
    if (! m_module)
    {
        error = message;
        return false;
    }

    for (size_t i = 0; i < m_module->n_serials; ++i)
    {
        if (m_module->serials[i].role == MHE_SERIAL_SYSTEM)
            m_systemChannel = m_module->serials[i].id;
        if (m_module->serials[i].role == MHE_SERIAL_CLI)
            m_cliChannel = m_module->serials[i].id;
    }
    return true;
}

bool Emulator::exposeTcp(const std::string &spec, std::string &error)
{
    const size_t eq = spec.find('=');
    if (! m_module || eq == std::string::npos)
    {
        error = "bad --tcp " + spec;
        return false;
    }

    const std::string name = spec.substr(0, eq);
    const int port = std::atoi(spec.c_str() + eq + 1);
    for (size_t i = 0; i < m_module->n_serials; ++i)
    {
        if (name != m_module->serials[i].name)
            continue;
        auto *line = new TcpLine { this, m_module->serials[i].id, nullptr };
        line->serial = mhe_tcp_serial_open(static_cast<uint16_t>(port), 0, tcpRx, line);
        if (! line->serial)
        {
            delete line;
            error = "cannot listen on port " + std::to_string(port);
            return false;
        }
        m_tcp[line->channel] = line;
        return true;
    }
    error = "unknown channel " + name;
    return false;
}

bool Emulator::start()
{
    if (! m_module)
        return false;

    if (m_cliChannel >= 0 && ! m_tcp.count(static_cast<uint8_t>(m_cliChannel)))
        m_console = mhe_console_create(consoleReply, this);
    if (m_systemChannel >= 0 && ! m_tcp.count(static_cast<uint8_t>(m_systemChannel)))
        m_sys = mhe_syscontrol_create(sysReply, this, m_stateDir.c_str(), m_module->device,
                                       std::getenv("MHE_SYS_DEBUG") != nullptr);

    if (m_module->start() != 0)
        return false;
    if (m_console)
        mhe_console_boot(m_console);
    return true;
}

void Emulator::stop()
{
    if (m_module)
        m_module->stop();
    for (auto &it : m_tcp)
        mhe_tcp_serial_close(it.second->serial);
    m_tcp.clear();
    m_module = nullptr;
}

void Emulator::turn(uint8_t encoder, int steps)
{
    if (m_module)
        m_module->encoder_turn(encoder, steps);
}

void Emulator::press(mhe_input_kind kind, uint8_t id, bool pressed)
{
    if (m_module)
        m_module->input_press(kind, id, pressed ? 1 : 0);
}

void Emulator::pot(uint8_t id, uint16_t value)
{
    if (m_module)
        m_module->pot_value(id, value);
}

/* ---- callbacks, on emulator threads ---- */

void Emulator::hostLog(void *h, mhe_log_level level, const char *message)
{
    static_cast<Emulator *>(h)->m_listener.log(level, message);
}

void Emulator::hostDisplay(void *h, uint8_t display, uint16_t w, uint16_t hgt, mhe_pixel_format, const uint8_t *bits,
                           uint8_t brightness)
{
    const size_t size = static_cast<size_t>(hgt) * ((w + 7u) / 8u);
    static_cast<Emulator *>(h)->m_listener.displayFrame(display, w, hgt, std::vector<uint8_t>(bits, bits + size),
                                                        brightness);
}

void Emulator::hostLed(void *h, uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    static_cast<Emulator *>(h)->m_listener.ledColor(led, r, g, b);
}

void Emulator::hostSerial(void *h, uint8_t channel, const uint8_t *data, size_t size)
{
    auto *self = static_cast<Emulator *>(h);

    auto it = self->m_tcp.find(channel);
    if (it != self->m_tcp.end())
        mhe_tcp_serial_send(it->second->serial, data, size);
    else if (channel == self->m_systemChannel && self->m_sys)
        mhe_syscontrol_feed(self->m_sys, data, size);
    else if (channel == self->m_cliChannel && self->m_console)
        mhe_console_feed(self->m_console, data, size);
}

void Emulator::sysReply(void *ctx, const uint8_t *data, size_t size)
{
    auto *self = static_cast<Emulator *>(ctx);
    if (self->m_module)
        self->m_module->serial_rx(static_cast<uint8_t>(self->m_systemChannel), data, size);
}

void Emulator::consoleReply(void *ctx, const uint8_t *data, size_t size)
{
    auto *self = static_cast<Emulator *>(ctx);
    if (self->m_module)
        self->m_module->serial_rx(static_cast<uint8_t>(self->m_cliChannel), data, size);
}

void Emulator::tcpRx(void *ctx, const uint8_t *data, size_t size)
{
    auto *line = static_cast<TcpLine *>(ctx);
    if (line->emulator->m_module)
        line->emulator->m_module->serial_rx(line->channel, data, size);
}
