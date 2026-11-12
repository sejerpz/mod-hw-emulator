/*
 * mod-hw-emulator — user interface
 *
 *   mod-hw-emulator [--state DIR] [--tcp CHANNEL=PORT]... MODULE
 *
 *   mod-hw-emulator [--skin DIR | --simple] ...
 *
 * --state names the state folder; by default mhe-state/<tag of the firmware>. The Firmware menu restarts on
 * another build of the same device found next to MODULE.
 *
 * The panel is built from what the module declares (displays, encoders, buttons, footswitches,
 * potentiometers, leds). With a skin of the device (a picture and the rectangle of each control, see findSkin)
 * SkinLayout puts them on the picture; without one, or with --simple, SimpleLayout stacks them: displays side
 * by side on top, encoders below them, then buttons, potentiometers (rows of four), footswitches.
 *
 * Keyboard: Q/A W/S E/D turn encoders 1-3 (anticlockwise/clockwise), 1 2 3 push them,
 *           Z X C V footswitches in panel order, F1-F4 buttons; keys pressed together go down together.
 * Mouse:    Ctrl+click arms a footswitch or button: the next click on another one presses both at once;
 *           wheel or drag turns a potentiometer, a double click puts it in the middle.
 */

#include "controls.h"
#include "emulator.h"
#include "../common/module_loader.h"

#include <QApplication>
#include <QBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QStatusBar>
#include <QMainWindow>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <map>
#include <memory>

namespace {

struct InputRef {
    mhe_input_kind kind;
    uint8_t id;
};

/* a picture of the device and the rectangle of each control on it, in its pixels */
struct Skin {
    QPixmap picture;
    int scale = 1;      /* picture pixels per window pixel at the nominal size */
    std::map<QString, QRect> rects;
    QStringList square; /* footswitches drawn as square buttons ("square" in the JSON) */
};

/*
 * DIR/DEVICE-panel.json lists the pictures of the panel at their sizes with the rectangles on each
 * (devices/dwarf/skin/README.md). The largest picture is taken, sharp when the window grows; the window
 * starts at the size of the smallest, or smaller to fit the screen (fitToScreen).
 */
bool loadSkin(const QString &dir, const QString &device, Skin &skin)
{
    QFile file(QDir(dir).filePath(device + QStringLiteral("-panel.json")));
    if (! file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject skins = root.value(QStringLiteral("skins")).toObject();
    for (const QJsonValue &name : root.value(QStringLiteral("square")).toArray())
        skin.square << name.toString();

    QString best;
    int bestWidth = 0, smallestWidth = 0;
    for (auto it = skins.begin(); it != skins.end(); ++it)
    {
        const int w = it.value().toObject().value(QStringLiteral("size")).toArray().at(0).toInt();
        if (w > bestWidth)
        {
            bestWidth = w;
            best = it.key();
        }
        if (! smallestWidth || w < smallestWidth)
            smallestWidth = w;
    }
    if (best.isEmpty() || ! skin.picture.load(QDir(dir).filePath(best)))
    {
        fprintf(stderr, "skin: cannot load the picture of %s\n", qPrintable(file.fileName()));
        return false;
    }
    skin.scale = std::max(1, bestWidth / smallestWidth);

    const QJsonObject rects = skins.value(best).toObject().value(QStringLiteral("rects_xywh")).toObject();
    for (auto it = rects.begin(); it != rects.end(); ++it)
    {
        const QJsonArray r = it.value().toArray();
        skin.rects[it.key()] = QRect(r.at(0).toInt(), r.at(1).toInt(), r.at(2).toInt(), r.at(3).toInt());
    }
    return true;
}

/* where a skin may be: next to the program (installed), or in the source tree (build/ next to devices/) */
bool findSkin(const QString &modulePath, const QString &device, Skin &skin)
{
    const QString app = QCoreApplication::applicationDirPath();
    const QString module = QFileInfo(modulePath).absolutePath();
    const QStringList dirs = {
        app + QStringLiteral("/skins/") + device,
        /* the macOS package: the program in the app bundle, the firmwares and skins next to it */
        module + QStringLiteral("/skins/") + device,
        app + QStringLiteral("/../devices/") + device + QStringLiteral("/skin"),
        module + QStringLiteral("/../devices/") + device + QStringLiteral("/skin"),
    };
    for (const QString &dir : dirs)
        if (loadSkin(dir, device, skin))
            return true;
    return false;
}

class Window : public QMainWindow, public Emulator::Listener
{
public:
    explicit Window(const QString &stateDir)
        : m_emulator(*this, stateDir.toStdString())
    {
        setWindowTitle(QStringLiteral("mod-hw-emulator"));
        setStyleSheet(QStringLiteral("QMainWindow { background: #26282c; } QStatusBar { color: #9aa; } "
                                     "QMenuBar { background: #26282c; color: #ccd; } "
                                     "QMenuBar::item:selected { background: #3a3d42; }"));
    }

    Emulator &emulator() { return m_emulator; }

    /* SKIN with a picture: SkinLayout; without: SimpleLayout */
    void build(const Skin &skin)
    {
        const mhe_module *m = m_emulator.module();
        const mhe_fw_info *fw = mhe_module_firmware(m);
        setWindowTitle(fw ? QStringLiteral("mod-hw-emulator — %1 %2 (%3)").arg(m->device, fw->describe, fw->date)
                          : QStringLiteral("mod-hw-emulator — %1 (%2)").arg(m->device, m->firmware));

        const bool skinned = ! skin.picture.isNull();
        PanelWidget *panel = skinned ? new PanelWidget(skin.picture, this) : nullptr;
        QWidget *central = skinned ? static_cast<QWidget *>(panel) : new QWidget(this);

        /* the controls, from what the module declares */
        for (size_t i = 0; i < m->n_displays; ++i)
        {
            auto *lcd = new LcdWidget(m->displays[i].width, m->displays[i].height, central);
            m_lcds[m->displays[i].id] = lcd;
        }

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
                m_encoders.push_back(w);
                m_byName[name] = w;
            }
            else if (in.kind == MHE_INPUT_BUTTON || in.kind == MHE_INPUT_FOOTSWITCH)
            {
                const bool foot = in.kind == MHE_INPUT_FOOTSWITCH;
                auto *w = new SwitchWidget(name, foot, central);
                const InputRef ref { in.kind, in.id };
                w->onPress = [this, w, ref](bool pressed) { switchPressed(w, ref, pressed); };
                (foot ? m_footswitches : m_buttons).push_back(w);
                m_byName[name] = w;
            }
            else if (in.kind == MHE_INPUT_POT)
            {
                auto *w = new PotWidget(name, central);
                const uint8_t id = in.id;
                w->onChange = [this, id](uint16_t value) { m_emulator.pot(id, value); };
                m_pots.push_back(w);
                m_byName[name] = w;
            }
        }

        /* a led goes where the skin draws "LED <name>", else on the control of the same name, else on its
         * own (SimpleLayout only: a skin with no place for it does not show it) */
        std::vector<LedWidget *> loose;
        for (size_t i = 0; i < m->n_leds; ++i)
        {
            const QString name = QString::fromUtf8(m->leds[i].name);
            const uint8_t id = m->leds[i].id;
            auto spot = skin.rects.find(QStringLiteral("LED ") + name);
            auto it = m_byName.find(name);

            if (skinned && spot != skin.rects.end())
            {
                auto *w = new LedWidget(name, central);
                w->setSkinned(true);
                panel->place(w, spot->second);
                m_ledSetters[id] = [w](const QColor &c) { w->setLed(c); };
            }
            else if (it != m_byName.end())
            {
                QWidget *w = it->second;
                m_ledSetters[id] = [w](const QColor &c) {
                    if (auto *e = dynamic_cast<EncoderWidget *>(w))
                        e->setLed(c);
                    else if (auto *s = dynamic_cast<SwitchWidget *>(w))
                        s->setLed(c);
                };
                m_ledSetters[id](Qt::black);
            }
            else if (! skinned)
            {
                auto *w = new LedWidget(name, central);
                loose.push_back(w);
                m_ledSetters[id] = [w](const QColor &c) { w->setLed(c); };
            }
        }

        if (skinned)
            skinLayout(*panel, skin);
        else
            simpleLayout(central, loose);
        setCentralWidget(central);

        statusBar()->showMessage(keysHint());

        /* switch keys pressed close together go down together, see key() */
        m_keyChord.setSingleShot(true);
        m_keyChord.setInterval(kChordWindowMs);
        connect(&m_keyChord, &QTimer::timeout, this, [this]() { pressPendingKeys(); });
    }

    /* SimpleLayout: displays on top, then encoders, buttons and loose leds, potentiometers, footswitches */
    void simpleLayout(QWidget *central, const std::vector<LedWidget *> &leds)
    {
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(24, 24, 24, 24);
        root->setSpacing(18);

        auto *displays = new QHBoxLayout();
        for (const auto &lcd : m_lcds)
            displays->addWidget(lcd.second, 1);
        root->addLayout(displays, 1);

        auto *encoders = new QHBoxLayout();
        auto *buttons = new QHBoxLayout();
        auto *footswitches = new QHBoxLayout();
        for (EncoderWidget *w : m_encoders)
            encoders->addWidget(w);
        for (SwitchWidget *w : m_buttons)
            buttons->addWidget(w);
        for (LedWidget *w : leds)
            buttons->addWidget(w);
        for (SwitchWidget *w : m_footswitches)
            footswitches->addWidget(w);

        root->addLayout(encoders);
        root->addLayout(buttons);
        for (size_t i = 0; i < m_pots.size(); i += 4)
        {
            auto *row = new QHBoxLayout();
            for (size_t j = i; j < i + 4 && j < m_pots.size(); ++j)
                row->addWidget(m_pots[j]);
            root->addLayout(row);
        }
        root->addLayout(footswitches);

        /* the display at 4x by default, less on a small screen */
        fitToScreen(sizeHint().expandedTo(QSize(620, 860)), 0);
    }

    /* SkinLayout: every control on the rectangle of the same name; the display on "display_window" */
    void skinLayout(PanelWidget &panel, const Skin &skin)
    {
        for (const auto &named : m_byName)
        {
            auto spot = skin.rects.find(named.first);
            if (spot == skin.rects.end())
            {
                fprintf(stderr, "skin: no place for %s\n", qPrintable(named.first));
                named.second->hide();
                continue;
            }
            if (auto *e = dynamic_cast<EncoderWidget *>(named.second))
                e->setSkinned(true);
            else if (auto *s = dynamic_cast<SwitchWidget *>(named.second))
            {
                s->setSkinned(true);
                s->setSquare(skin.square.contains(named.first));
            }
            else if (auto *pot = dynamic_cast<PotWidget *>(named.second))
                pot->setSkinned(true);
            panel.place(named.second, spot->second);
        }

        /* the displays in the order of their ids: the first one goes on "display", the next on "display 2"
         * and so on; each takes the whole window around it ("display_window"), so the glass shows no frame
         * of the picture */
        int n = 0;
        for (const auto &lcd : m_lcds)
        {
            const QString name = n++ ? QStringLiteral("display %1").arg(n) : QStringLiteral("display");
            auto spot = skin.rects.find(name + QStringLiteral("_window"));
            if (spot == skin.rects.end())
                spot = skin.rects.find(name);
            if (spot == skin.rects.end())
            {
                lcd.second->hide();
                continue;
            }
            lcd.second->setSkinned(true);
            panel.place(lcd.second, spot->second);
        }

        /* the picture at its nominal size, less on a small screen */
        fitToScreen(skin.picture.size() / skin.scale,
                    statusBar()->sizeHint().height() + (menuWidget() ? menuWidget()->sizeHint().height() : 0));
    }

    /* the window at SIZE (plus EXTRA rows for the menu and the status bar), made smaller, as it is, when it
     * would take more than about half the width or two thirds of the height of the screen: it sits next to
     * MOD Desktop, on a laptop too; the user can make it larger */
    void fitToScreen(const QSize &size, int extra)
    {
        QSize fitted = size;
        if (const QScreen *screen = QGuiApplication::primaryScreen())
        {
            const QRect free = screen->availableGeometry();
            const double factor = std::min({1.0, free.width() * 0.45 / size.width(),
                                            (free.height() * 0.65 - extra) / size.height()});
            if (factor > 0)
                fitted = size * factor;
        }
        resize(fitted.width(), fitted.height() + extra);
    }

    /* Firmware menu: the builds of the same device next to the module (fw-dwarf-<describe>.so, see the
     * Makefile), newest first; a module cannot start twice in a process, so choosing one calls `choose`,
     * which starts a new process on it */
    void firmwareMenu(const QString &modulePath, const QString &device, std::function<void(const QString &)> choose)
    {
        const QFileInfo current(modulePath);
        const QString prefix = QStringLiteral("fw-%1-").arg(device);
        const QString suffix = QLatin1Char('.') + current.suffix();
        const QFileInfoList builds = current.absoluteDir().entryInfoList(
            QStringList(prefix + QLatin1Char('*') + suffix), QDir::Files, QDir::Time);

        QMenu *menu = menuBar()->addMenu(QStringLiteral("&Firmware"));
        for (const QFileInfo &build : builds)
        {
            const QString name = build.fileName().mid(prefix.size()).chopped(suffix.size());
            QAction *action = menu->addAction(name);
            action->setCheckable(true);
            const bool running = build.canonicalFilePath() == current.canonicalFilePath();
            action->setChecked(running);
            if (running)
                action->setEnabled(false);
            const QString path = build.absoluteFilePath();
            connect(action, &QAction::triggered, this, [choose, path]() { choose(path); });
        }
        if (builds.isEmpty())
            menu->addAction(QStringLiteral("no %1*%2 next to the module").arg(prefix, suffix))->setEnabled(false);
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
    /* the keys of key(), for the controls this device has */
    QString keysHint() const
    {
        static const char *const turn[] = { "Q/A", "W/S", "E/D" };
        static const char *const push[] = { "1", "2", "3" };
        static const char *const foot[] = { "Z", "X", "C", "V" };
        static const char *const button[] = { "F1", "F2", "F3", "F4" };
        auto list = [](const char *const *keys, size_t n, size_t max) {
            QStringList l;
            for (size_t i = 0; i < n && i < max; ++i)
                l << QString::fromLatin1(keys[i]);
            return l.join(QLatin1Char(' '));
        };

        QStringList parts;
        if (! m_encoders.empty())
            parts << list(turn, m_encoders.size(), 3) + QStringLiteral(" turn, ")
                     + list(push, m_encoders.size(), 3) + QStringLiteral(" push");
        if (! m_footswitches.empty())
            parts << list(foot, m_footswitches.size(), 4) + QStringLiteral(" footswitches");
        if (! m_buttons.empty())
            parts << QStringLiteral("%1-%2 buttons").arg(QLatin1String(button[0]),
                                                         QLatin1String(button[qMin<size_t>(m_buttons.size(), 4) - 1]));
        QString hint = parts.join(QStringLiteral(", "));
        if (! m_pots.empty())
            hint += QStringLiteral("; wheel or drag on a potentiometer, double click centres it");
        else if (m_footswitches.size() + m_buttons.size() > 1)
            hint += QStringLiteral("; Ctrl+click arms a switch to press it with the next one");
        return hint;
    }

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
        static const int footKeys[] = { Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V };
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
        SwitchWidget *s = nullptr;
        for (size_t i = 0; i < 4 && i < m_footswitches.size(); ++i)
            if (k == footKeys[i])
                s = m_footswitches[i];
        for (size_t i = 0; i < 4 && i < m_buttons.size(); ++i)
            if (k == buttonKeys[i])
                s = m_buttons[i];
        if (s)
            switchKey(s, down);
    }

    /*
     * Two footswitches count as pressed together on the Dwarf when they go down within 50 ms
     * (BUTTON_DOUBLE_PRESS_DEBOUNCE), which is how the footswitch pages are reached. Two keys never go down
     * that close by chance, so the keys pressed within kChordWindowMs of each other are held back and sent
     * together.
     */
    static constexpr int kChordWindowMs = 40;

    void switchKey(SwitchWidget *s, bool down)
    {
        auto pending = std::find(m_pendingKeys.begin(), m_pendingKeys.end(), s);

        if (down)
        {
            if (pending == m_pendingKeys.end() && ! s->isPressed())
                m_pendingKeys.push_back(s);
            if (! m_keyChord.isActive())
                m_keyChord.start();
            return;
        }

        /* let go before the window closed: it still went down, with whatever came along */
        if (pending != m_pendingKeys.end())
        {
            m_keyChord.stop();
            pressPendingKeys();
        }
        s->setPressed(false);
    }

    void pressPendingKeys()
    {
        const std::vector<SwitchWidget *> keys = std::move(m_pendingKeys);
        m_pendingKeys.clear();
        for (size_t i = 0; i < keys.size(); ++i)
        {
            if (i)
                QThread::msleep(kChordStaggerMs);
            keys[i]->setPressed(true);
        }
    }

    /* the Dwarf misses two presses in the same 1 ms scan: the switches of a chord go down a few ms apart */
    static constexpr int kChordStaggerMs = 8;

    /* a switch going down takes the armed ones (Ctrl+click) down with it, and they go up with it */
    void switchPressed(SwitchWidget *w, const InputRef &ref, bool pressed)
    {
        if (pressed)
        {
            std::vector<SwitchWidget *> armed;
            for (SwitchWidget *o : allSwitches())
                if (o != w && o->isArmed())
                    armed.push_back(o);
            for (SwitchWidget *o : armed)
            {
                o->setArmed(false);
                m_chordOf[w].push_back(o);
                o->setPressed(true);
                QThread::msleep(kChordStaggerMs);
            }
            m_emulator.press(ref.kind, ref.id, true);
        }
        else
        {
            m_emulator.press(ref.kind, ref.id, false);
            const std::vector<SwitchWidget *> along = std::move(m_chordOf[w]);
            m_chordOf.erase(w);
            for (SwitchWidget *o : along)
                o->setPressed(false);
        }
    }

    std::vector<SwitchWidget *> allSwitches() const
    {
        std::vector<SwitchWidget *> all(m_footswitches);
        all.insert(all.end(), m_buttons.begin(), m_buttons.end());
        return all;
    }

    Emulator m_emulator;
    QTimer m_keyChord;
    std::vector<SwitchWidget *> m_pendingKeys;
    std::map<SwitchWidget *, std::vector<SwitchWidget *>> m_chordOf;
    std::map<uint8_t, LcdWidget *> m_lcds;
    std::vector<EncoderWidget *> m_encoders;
    std::vector<SwitchWidget *> m_footswitches;
    std::vector<SwitchWidget *> m_buttons;
    std::vector<PotWidget *> m_pots;
    std::map<QString, QWidget *> m_byName;
    std::map<uint8_t, std::function<void(const QColor &)>> m_ledSetters;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QString stateDir;
    QStringList tcp;
    QString modulePath;
    int moduleArg = -1;
    QString screenshot;
    int screenshotAfter = 3000;
    QString skinDir;
    bool simple = false;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        if (args[i] == QLatin1String("--state") && i + 1 < args.size())
            stateDir = args[++i];
        else if (args[i] == QLatin1String("--skin") && i + 1 < args.size())
            skinDir = args[++i];
        else if (args[i] == QLatin1String("--simple"))
            simple = true;
        else if (args[i] == QLatin1String("--tcp") && i + 1 < args.size())
            tcp << args[++i];
        else if (args[i] == QLatin1String("--screenshot") && i + 1 < args.size())
            screenshot = args[++i];
        else if (args[i] == QLatin1String("--after") && i + 1 < args.size())
            screenshotAfter = args[++i].toInt();
        else
        {
            modulePath = args[i];
            moduleArg = i;
        }
    }

    if (modulePath.isEmpty())
    {
        fprintf(stderr, "usage: mod-hw-emulator [--state DIR] [--tcp CHANNEL=PORT]... [--skin DIR | --simple] MODULE\n");
        return 2;
    }

    /* each firmware tag keeps its own state, the EEPROM layout may change between them */
    if (stateDir.isEmpty())
    {
        char dir[512];
        mhe_default_state_dir("mhe-state", mhe_module_info(qPrintable(modulePath)), dir, sizeof(dir));
        stateDir = QString::fromLocal8Bit(dir);
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

    /* the same arguments on another firmware build, started once this process has let go of its ports */
    const QString device = QString::fromUtf8(window.emulator().module()->device);
    QString nextModule;
    window.firmwareMenu(modulePath, device, [&nextModule](const QString &path) {
        nextModule = path;
        QApplication::quit();
    });

    Skin skin;
    if (! simple)
    {
        const bool found = skinDir.isEmpty() ? findSkin(modulePath, device, skin) : loadSkin(skinDir, device, skin);
        if (! found && ! skinDir.isEmpty())
            fprintf(stderr, "skin: none for %s in %s\n", qPrintable(device), qPrintable(skinDir));
    }
    window.build(skin);
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
    if (! nextModule.isEmpty())
    {
        QStringList next = args.mid(1);
        next[moduleArg - 1] = nextModule;
        if (! QProcess::startDetached(QCoreApplication::applicationFilePath(), next))
            fprintf(stderr, "cannot start %s\n", qPrintable(QCoreApplication::applicationFilePath()));
    }
    /* the firmware tasks are parked, not joined */
    fflush(stdout);
    _Exit(rc);
}
