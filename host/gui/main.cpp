/*
 * mod-hw-emulator — user interface
 *
 *   mod-hw-emulator [--state DIR] [--tcp CHANNEL=PORT]... MODULE
 *
 * The panel is built from what the module declares (displays, encoders, buttons, footswitches, leds), laid
 * out by SimpleLayout: display on top, encoders below it, then buttons, then footswitches. A skin layout
 * will place the same controls on a picture of the device.
 *
 * Keyboard: Q/A W/S E/D turn encoders 1-3 (anticlockwise/clockwise), 1 2 3 push them,
 *           Z X C footswitches in panel order, F1-F4 buttons.
 */

#include "controls.h"
#include "emulator.h"

#include <QApplication>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QMainWindow>
#include <QTimer>

#include <map>
#include <memory>

namespace {

struct InputRef {
    mhe_input_kind kind;
    uint8_t id;
};

class Window : public QMainWindow, public Emulator::Listener
{
public:
    explicit Window(const QString &stateDir)
        : m_emulator(*this, stateDir.toStdString())
    {
        setWindowTitle(QStringLiteral("mod-hw-emulator"));
        setStyleSheet(QStringLiteral("QMainWindow { background: #26282c; } QStatusBar { color: #9aa; }"));
    }

    Emulator &emulator() { return m_emulator; }

    void build()
    {
        const mhe_module *m = m_emulator.module();
        setWindowTitle(QStringLiteral("mod-hw-emulator — %1 (%2)").arg(m->device, m->firmware));

        auto *central = new QWidget(this);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(24, 24, 24, 24);
        root->setSpacing(18);

        /* SimpleLayout */
        for (size_t i = 0; i < m->n_displays; ++i)
        {
            auto *lcd = new LcdWidget(m->displays[i].width, m->displays[i].height, central);
            m_lcds[m->displays[i].id] = lcd;
            root->addWidget(lcd, 1);
        }

        auto *encoders = new QHBoxLayout();
        auto *buttons = new QHBoxLayout();
        auto *footswitches = new QHBoxLayout();

        for (size_t i = 0; i < m->n_inputs; ++i)
        {
            const mhe_input_info &in = m->inputs[i];
            const QString name = QString::fromUtf8(in.name);

            if (in.kind == MHE_INPUT_ENCODER)
            {
                auto *w = new EncoderWidget(name, central);
                const uint8_t id = in.id;
                w->onTurn = [this, id](int steps) { m_emulator.turn(id, steps); };
                w->onPress = [this, id](bool pressed) { m_emulator.press(MHE_INPUT_ENCODER, id, pressed); };
                encoders->addWidget(w);
                m_encoders.push_back(w);
                m_byName[name] = w;
            }
            else if (in.kind == MHE_INPUT_BUTTON || in.kind == MHE_INPUT_FOOTSWITCH)
            {
                const bool foot = in.kind == MHE_INPUT_FOOTSWITCH;
                auto *w = new SwitchWidget(name, foot, central);
                const InputRef ref { in.kind, in.id };
                w->onPress = [this, ref](bool pressed) { m_emulator.press(ref.kind, ref.id, pressed); };
                (foot ? footswitches : buttons)->addWidget(w);
                (foot ? m_footswitches : m_buttons).push_back(w);
                m_byName[name] = w;
            }
        }

        /* leds go on the control of the same name, the others stand alone */
        auto *leds = new QHBoxLayout();
        for (size_t i = 0; i < m->n_leds; ++i)
        {
            const QString name = QString::fromUtf8(m->leds[i].name);
            auto it = m_byName.find(name);
            if (it != m_byName.end())
            {
                QWidget *w = it->second;
                m_ledSetters[m->leds[i].id] = [w](const QColor &c) {
                    if (auto *e = dynamic_cast<EncoderWidget *>(w))
                        e->setLed(c);
                    else if (auto *s = dynamic_cast<SwitchWidget *>(w))
                        s->setLed(c);
                };
                m_ledSetters[m->leds[i].id](Qt::black);
            }
            else
            {
                auto *w = new LedWidget(name, central);
                leds->addWidget(w);
                m_ledSetters[m->leds[i].id] = [w](const QColor &c) { w->setLed(c); };
            }
        }

        if (! leds->isEmpty())
            buttons->addLayout(leds);

        root->addLayout(encoders);
        root->addLayout(buttons);
        root->addLayout(footswitches);
        setCentralWidget(central);

        /* the display at 4x by default */
        resize(sizeHint().expandedTo(QSize(620, 860)));

        statusBar()->showMessage(QStringLiteral("Q/A W/S E/D turn, 1 2 3 push, Z X C footswitches, F1-F4 buttons"));
    }

    /* ---- Emulator::Listener, on emulator threads: hand over to the GUI thread ---- */

    void displayFrame(uint8_t display, uint16_t width, uint16_t height, std::vector<uint8_t> bits,
                      uint8_t brightness) override
    {
        QMetaObject::invokeMethod(this, [this, display, width, height, bits, brightness]() {
            auto it = m_lcds.find(display);
            if (it != m_lcds.end())
                it->second->setFrame(width, height, bits, brightness);
        }, Qt::QueuedConnection);
    }

    void ledColor(uint8_t led, uint8_t r, uint8_t g, uint8_t b) override
    {
        QMetaObject::invokeMethod(this, [this, led, r, g, b]() {
            auto it = m_ledSetters.find(led);
            if (it != m_ledSetters.end())
                it->second(QColor(r, g, b));
        }, Qt::QueuedConnection);
    }

    void log(mhe_log_level level, const std::string &message) override
    {
        fprintf(stderr, "[fw] %s\n", message.c_str());
        if (level <= MHE_LOG_WARNING)
        {
            const QString text = QString::fromStdString(message);
            QMetaObject::invokeMethod(this, [this, text]() { statusBar()->showMessage(text, 10000); },
                                      Qt::QueuedConnection);
        }
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (! e->isAutoRepeat() || isTurnKey(e->key()))
            key(e->key(), true);
    }

    void keyReleaseEvent(QKeyEvent *e) override
    {
        if (! e->isAutoRepeat())
            key(e->key(), false);
    }

private:
    static bool isTurnKey(int k)
    {
        return k == Qt::Key_Q || k == Qt::Key_A || k == Qt::Key_W || k == Qt::Key_S || k == Qt::Key_E
               || k == Qt::Key_D;
    }

    void key(int k, bool down)
    {
        static const int turnKeys[][2] = { { Qt::Key_Q, Qt::Key_A }, { Qt::Key_W, Qt::Key_S },
                                           { Qt::Key_E, Qt::Key_D } };
        static const int pushKeys[] = { Qt::Key_1, Qt::Key_2, Qt::Key_3 };
        static const int footKeys[] = { Qt::Key_Z, Qt::Key_X, Qt::Key_C };
        static const int buttonKeys[] = { Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4 };

        for (size_t i = 0; i < 3 && i < m_encoders.size(); ++i)
        {
            if (down && k == turnKeys[i][0])
                m_encoders[i]->turnFromKeyboard(-1);
            if (down && k == turnKeys[i][1])
                m_encoders[i]->turnFromKeyboard(1);
            if (k == pushKeys[i] && m_encoders[i]->onPress)
                m_encoders[i]->onPress(down);
        }
        for (size_t i = 0; i < 3 && i < m_footswitches.size(); ++i)
            if (k == footKeys[i])
                m_footswitches[i]->setPressed(down);
        for (size_t i = 0; i < 4 && i < m_buttons.size(); ++i)
            if (k == buttonKeys[i])
                m_buttons[i]->setPressed(down);
    }

    Emulator m_emulator;
    std::map<uint8_t, LcdWidget *> m_lcds;
    std::vector<EncoderWidget *> m_encoders;
    std::vector<SwitchWidget *> m_footswitches;
    std::vector<SwitchWidget *> m_buttons;
    std::map<QString, QWidget *> m_byName;
    std::map<uint8_t, std::function<void(const QColor &)>> m_ledSetters;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QString stateDir = QStringLiteral("mhe-state");
    QStringList tcp;
    QString modulePath;
    QString screenshot;
    int screenshotAfter = 3000;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        if (args[i] == QLatin1String("--state") && i + 1 < args.size())
            stateDir = args[++i];
        else if (args[i] == QLatin1String("--tcp") && i + 1 < args.size())
            tcp << args[++i];
        else if (args[i] == QLatin1String("--screenshot") && i + 1 < args.size())
            screenshot = args[++i];
        else if (args[i] == QLatin1String("--after") && i + 1 < args.size())
            screenshotAfter = args[++i].toInt();
        else
            modulePath = args[i];
    }

    if (modulePath.isEmpty())
    {
        fprintf(stderr, "usage: mod-hw-emulator [--state DIR] [--tcp CHANNEL=PORT]... MODULE\n");
        return 2;
    }

    Window window(stateDir);
    std::string error;
    if (! window.emulator().load(modulePath.toStdString(), error))
    {
        QMessageBox::critical(nullptr, QStringLiteral("mod-hw-emulator"), QString::fromStdString(error));
        return 1;
    }
    for (const QString &spec : tcp)
    {
        if (! window.emulator().exposeTcp(spec.toStdString(), error))
        {
            QMessageBox::critical(nullptr, QStringLiteral("mod-hw-emulator"), QString::fromStdString(error));
            return 1;
        }
    }

    window.build();
    window.show();

    if (! window.emulator().start())
    {
        QMessageBox::critical(&window, QStringLiteral("mod-hw-emulator"), QStringLiteral("The firmware did not start"));
        return 1;
    }

    /* --screenshot FILE.png --after MS: saves the window and quits (for docs and checks) */
    if (! screenshot.isEmpty())
    {
        QTimer::singleShot(screenshotAfter, &window, [&window, screenshot]() {
            window.grab().save(screenshot);
            QApplication::quit();
        });
    }

    const int rc = app.exec();
    window.emulator().stop();
    /* the firmware tasks are parked, not joined */
    fflush(stdout);
    _Exit(rc);
}
