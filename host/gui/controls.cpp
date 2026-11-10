/*
 * mod-hw-emulator — panel controls
 */

#include "controls.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

#ifndef M_PI   /* not in strict C++ (MinGW's -std=c++17) */
#define M_PI 3.14159265358979323846
#endif

namespace {

const QColor kPanel(38, 40, 44);
const QColor kMetal(150, 154, 160);
const QColor kLabel(200, 204, 210);

QColor ledOrOff(const QColor &c)
{
    return (c.red() + c.green() + c.blue()) < 8 ? QColor(25, 25, 28) : c;
}

/* a led looks brighter than its duty cycle: stretch it */
QColor ledLook(const QColor &c)
{
    auto boost = [](int v) { return std::min(255, static_cast<int>(std::sqrt(v / 255.0) * 255.0)); };
    return QColor(boost(c.red()), boost(c.green()), boost(c.blue()));
}

void drawLabel(QPainter &p, const QRect &r, const QString &text)
{
    p.setPen(kLabel);
    QFont f = p.font();
    f.setPointSizeF(8);
    p.setFont(f);
    p.drawText(r, Qt::AlignHCenter | Qt::AlignBottom, text);
}

} // namespace

/* ---- LCD ---- */

LcdWidget::LcdWidget(int width, int height, QWidget *parent)
    : QWidget(parent), m_width(width), m_height(height),
      m_bits(static_cast<size_t>(height) * ((width + 7) / 8))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LcdWidget::setFrame(int width, int height, const std::vector<uint8_t> &mono1, int brightness)
{
    m_width = width;
    m_height = height;
    m_bits = mono1;
    m_brightness = brightness;
    update();
}

QSize LcdWidget::sizeHint() const
{
    return QSize(m_width * 4 + 24, m_height * 4 + 24);
}

QSize LcdWidget::minimumSizeHint() const
{
    return QSize(m_width * 2 + 24, m_height * 2 + 24);
}

void LcdWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    const double k = 0.25 + 0.75 * m_brightness / 255.0;
    const QColor lit(static_cast<int>(235 * k), static_cast<int>(240 * k), static_cast<int>(245 * k));
    const QColor off(14, 15, 18);
    const int stride = (m_width + 7) / 8;

    /* skinned: the whole window of the picture is glass, the pixels as large as it lets them, centred */
    if (m_skinned)
    {
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath glass;
        const double radius = width() * 0.02;
        glass.addRoundedRect(QRectF(rect()), radius, radius);
        p.fillPath(glass, off);
        p.setRenderHint(QPainter::Antialiasing, false);

        const double s = std::min(width() / double(m_width), height() / double(m_height));
        const double x0 = (width() - s * m_width) / 2, y0 = (height() - s * m_height) / 2;
        const double gap = s >= 3 ? 0.6 : 0;
        for (int y = 0; y < m_height; ++y)
            for (int x = 0; x < m_width; ++x)
                if ((m_bits[static_cast<size_t>(y * stride + x / 8)] >> (7 - x % 8)) & 1)
                    p.fillRect(QRectF(x0 + x * s, y0 + y * s, s - gap, s - gap), lit);
        return;
    }

    p.fillRect(rect(), kPanel);

    const int scale = std::max(1, std::min((width() - 24) / m_width, (height() - 24) / m_height));
    const int w = m_width * scale, h = m_height * scale;
    const QRect screen((width() - w) / 2, (height() - h) / 2, w, h);

    /* bezel */
    p.fillRect(screen.adjusted(-10, -10, 10, 10), QColor(12, 12, 14));

    /* negative LCD: lit pixels show the backlight, the rest stays dark */
    p.fillRect(screen, off);

    const int gap = scale >= 4 ? 1 : 0;
    for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x)
            if ((m_bits[static_cast<size_t>(y * stride + x / 8)] >> (7 - x % 8)) & 1)
                p.fillRect(screen.x() + x * scale, screen.y() + y * scale, scale - gap, scale - gap, lit);
}

/* ---- encoder ---- */

EncoderWidget::EncoderWidget(const QString &name, QWidget *parent)
    : QWidget(parent), m_name(name)
{
    setCursor(Qt::OpenHandCursor);
    setToolTip(name + QStringLiteral(": wheel or drag to turn, click to push"));
    setMinimumSize(sizeHint());
}

QSize EncoderWidget::sizeHint() const
{
    return QSize(96, 112);
}

void EncoderWidget::setLed(const QColor &color)
{
    m_led = color;
    update();
}

void EncoderWidget::turnFromKeyboard(int steps)
{
    m_angle += steps;
    update();
    if (onTurn)
        onTurn(steps);
}

void EncoderWidget::setSkinned(bool skinned)
{
    m_skinned = skinned;
    setMinimumSize(skinned ? QSize(8, 8) : sizeHint());
}

void EncoderWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* pointer angle: 20 detents per turn, 0 at the top */
    const double a = (m_angle % 20) * (2 * M_PI / 20) - M_PI / 2;

    /* skinned: the rectangle is the collar of the knob in the picture (radius R, see mkskin.py); the cap
     * (0.56 R) is drawn again to cover the pointer of the picture, and the turning pointer over it */
    if (m_skinned)
    {
        const QPointF c(width() / 2.0, height() / 2.0);
        const double R = std::min(width(), height()) / 2.0;

        if (m_led.red() + m_led.green() + m_led.blue() >= 8)
        {
            p.setPen(QPen(ledLook(m_led), R * 0.09));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, R * 0.93, R * 0.93);
        }

        /* the colours of the cap in mkskin.py */
        p.setPen(QPen(QColor(0x6c, 0x6f, 0x76), R * 0.03));
        p.setBrush(m_pressed ? QColor(0x2a, 0x2b, 0x30) : QColor(0x46, 0x48, 0x4e));
        p.drawEllipse(c, R * 0.557, R * 0.557);

        const QPointF u(std::cos(a), std::sin(a));
        p.setPen(QPen(QColor(0xd6, 0xd8, 0xdc), R * 0.057, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + u * R * 0.264, c + u * R * 0.509);
        return;
    }

    const int d = std::min(width(), height() - 20) - 12;
    const QRectF knob((width() - d) / 2.0, 6, d, d);

    /* led ring */
    p.setPen(QPen(ledLook(ledOrOff(m_led)), 4));
    p.drawEllipse(knob.adjusted(-3, -3, 3, 3));

    /* knob */
    QRadialGradient g(knob.center() - QPointF(d / 6.0, d / 6.0), d * 0.7);
    g.setColorAt(0, m_pressed ? kMetal.darker(130) : kMetal.lighter(120));
    g.setColorAt(1, kMetal.darker(170));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(knob);

    /* pointer */
    const QPointF c = knob.center();
    p.setPen(QPen(QColor(30, 30, 30), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c + QPointF(std::cos(a), std::sin(a)) * d * 0.15, c + QPointF(std::cos(a), std::sin(a)) * d * 0.42);

    drawLabel(p, rect(), m_name);
}

void EncoderWidget::wheelEvent(QWheelEvent *e)
{
    m_wheelRest += e->angleDelta().y();
    const int steps = m_wheelRest / 120;
    m_wheelRest %= 120;
    if (steps)
        turnFromKeyboard(steps);
    e->accept();
}

void EncoderWidget::mousePressEvent(QMouseEvent *e)
{
    m_dragY = e->pos().y();
    m_dragged = 0;
    m_pressed = true;
    setCursor(Qt::ClosedHandCursor);
    if (onPress)
        onPress(true);
    update();
}

void EncoderWidget::mouseMoveEvent(QMouseEvent *e)
{
    /* dragging turns: one step every 12 pixels, up is clockwise */
    const int steps = (m_dragY - e->pos().y()) / 12;
    if (! steps)
        return;
    m_dragY -= steps * 12;
    if (! m_dragged && m_pressed && onPress)
    {
        /* a drag is a turn, not a push */
        m_pressed = false;
        onPress(false);
    }
    m_dragged += steps;
    turnFromKeyboard(steps);
}

void EncoderWidget::mouseReleaseEvent(QMouseEvent *)
{
    setCursor(Qt::OpenHandCursor);
    if (m_pressed && onPress)
        onPress(false);
    m_pressed = false;
    update();
}

/* ---- potentiometer ---- */

PotWidget::PotWidget(const QString &name, QWidget *parent)
    : QWidget(parent), m_name(name)
{
    setCursor(Qt::OpenHandCursor);
    setToolTip(name + QStringLiteral(": wheel or drag to turn, double click for the middle"));
    setMinimumSize(sizeHint());
}

QSize PotWidget::sizeHint() const
{
    return QSize(80, 96);
}

void PotWidget::setSkinned(bool skinned)
{
    m_skinned = skinned;
    setMinimumSize(skinned ? QSize(8, 8) : sizeHint());
}

void PotWidget::setValue(int value)
{
    value = std::max(0, std::min(65535, value));
    if (value == m_value)
        return;
    m_value = static_cast<uint16_t>(value);
    setToolTip(QStringLiteral("%1: %2% (wheel or drag to turn, double click for the middle)")
                   .arg(m_name).arg(m_value * 100 / 65535));
    update();
    if (onChange)
        onChange(m_value);
}

void PotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* travel: -135 degrees (7 o'clock) to +135 (5 o'clock), clockwise from 12 o'clock */
    const double t = (-135.0 + 270.0 * m_value / 65535.0) * M_PI / 180.0;
    const QPointF u(std::sin(t), -std::cos(t));
    const int spanQt = -static_cast<int>(270.0 * 16 * m_value / 65535.0);

    /* skinned: the rectangle is the collar of the knob in the picture (radius R, see mkskin.py of the Duo X):
     * the travel is lit on the ring at 0.84 R, the pointer drawn on the knob (0.66 R) */
    if (m_skinned)
    {
        const QPointF c(width() / 2.0, height() / 2.0);
        const double R = std::min(width(), height()) / 2.0;
        const QRectF ring(c.x() - R * 0.84, c.y() - R * 0.84, R * 1.68, R * 1.68);

        p.setPen(QPen(QColor(0xd6, 0xd8, 0xdc), R * 0.07, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(ring, 225 * 16, spanQt);

        p.setPen(QPen(QColor(0xe8, 0xea, 0xee), R * 0.07, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + u * R * 0.2, c + u * R * 0.58);
        return;
    }

    const int d = std::min(width(), height() - 20) - 16;
    const QRectF knob((width() - d) / 2.0, 8, d, d);
    const QPointF c = knob.center();

    /* the whole travel, then the part up to the value */
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(60, 62, 68), 3, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(knob.adjusted(-4, -4, 4, 4), 225 * 16, -270 * 16);
    p.setPen(QPen(kLabel, 3, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(knob.adjusted(-4, -4, 4, 4), 225 * 16, spanQt);

    QRadialGradient g(c - QPointF(d / 6.0, d / 6.0), d * 0.7);
    g.setColorAt(0, QColor(70, 72, 78));
    g.setColorAt(1, QColor(24, 25, 28));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(knob.adjusted(3, 3, -3, -3));

    p.setPen(QPen(QColor(230, 232, 236), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c + u * d * 0.12, c + u * d * 0.4);

    drawLabel(p, rect(), m_name);
}

void PotWidget::wheelEvent(QWheelEvent *e)
{
    /* a notch of the wheel is 1/64 of the travel */
    m_wheelRest += e->angleDelta().y();
    const int notches = m_wheelRest / 120;
    m_wheelRest %= 120;
    if (notches)
        setValue(m_value + notches * 1024);
    e->accept();
}

void PotWidget::mousePressEvent(QMouseEvent *e)
{
    m_dragY = e->pos().y();
    m_dragValue = m_value;
    setCursor(Qt::ClosedHandCursor);
}

void PotWidget::mouseMoveEvent(QMouseEvent *e)
{
    /* up turns clockwise; the whole travel in 200 pixels */
    setValue(m_dragValue + (m_dragY - e->pos().y()) * 65535 / 200);
}

void PotWidget::mouseDoubleClickEvent(QMouseEvent *)
{
    setValue(32768);
}

/* ---- switch ---- */

SwitchWidget::SwitchWidget(const QString &name, bool footswitch, QWidget *parent)
    : QWidget(parent), m_name(name), m_footswitch(footswitch)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(name);
    setMinimumSize(sizeHint());
}

QSize SwitchWidget::sizeHint() const
{
    return m_footswitch ? QSize(110, 120) : QSize(70, 70);
}

void SwitchWidget::setLed(const QColor &color)
{
    m_led = color;
    m_hasLed = true;
    update();
}

void SwitchWidget::setPressed(bool pressed)
{
    if (pressed == m_pressed)
        return;
    m_pressed = pressed;
    update();
    if (onPress)
        onPress(pressed);
}

void SwitchWidget::setSkinned(bool skinned)
{
    m_skinned = skinned;
    setMinimumSize(skinned ? QSize(8, 8) : sizeHint());
}

void SwitchWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* skinned: the picture draws the switch, a pressed one is shaded and an armed one ringed. A footswitch
     * rectangle holds its hexagon nut (radius R), the round cap is 0.76 R; a button fills its rectangle */
    if (m_skinned)
    {
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        const QPointF c = r.center();
        const double R = std::min(r.width(), r.height()) / 2.0;
        const double round = std::min(r.width(), r.height()) * 0.14;
        const bool nut = m_footswitch && ! m_square;

        if (m_pressed)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 110));
            if (nut)
                p.drawEllipse(c, R * 0.76, R * 0.76);
            else
                p.drawRoundedRect(r, round, round);
        }
        if (m_armed)
        {
            p.setPen(QPen(QColor(255, 190, 40), std::max(2.0, R * 0.06), Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            if (nut)
                p.drawEllipse(c, R * 0.95, R * 0.95);
            else
                p.drawRoundedRect(r.adjusted(1, 1, -1, -1), round, round);
        }
        return;
    }

    const int d = std::min(width(), height() - 20) - (m_footswitch ? 20 : 24);
    const QRectF body((width() - d) / 2.0, m_footswitch ? 24 : 6, d, d);

    if (m_hasLed)
    {
        const QRectF led(width() / 2.0 - 7, m_footswitch ? 4 : body.bottom() - 2, 14, 14);
        p.setPen(QPen(QColor(10, 10, 10), 1));
        p.setBrush(ledLook(ledOrOff(m_led)));
        p.drawEllipse(m_footswitch ? led : QRectF(body.right() - 6, body.top() - 4, 12, 12));
    }

    QRadialGradient g(body.center(), d * 0.6);
    g.setColorAt(0, m_pressed ? kMetal.darker(150) : kMetal);
    g.setColorAt(1, kMetal.darker(m_pressed ? 220 : 180));
    p.setPen(QPen(QColor(20, 20, 22), 2));
    p.setBrush(g);
    p.drawEllipse(body.adjusted(m_pressed ? 2 : 0, m_pressed ? 2 : 0, m_pressed ? -2 : 0, m_pressed ? -2 : 0));

    /* armed for a press together with another switch */
    if (m_armed)
    {
        p.setPen(QPen(QColor(255, 190, 40), 3, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(body.adjusted(-4, -4, 4, 4));
    }

    drawLabel(p, rect(), m_name);
}

void SwitchWidget::setArmed(bool armed)
{
    if (armed == m_armed)
        return;
    m_armed = armed;
    update();
}

void SwitchWidget::mousePressEvent(QMouseEvent *e)
{
    /* Ctrl+click arms (or disarms) the switch instead of pressing it */
    if (e->modifiers() & Qt::ControlModifier)
    {
        setArmed(! m_armed);
        return;
    }
    setPressed(true);
}

void SwitchWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier && ! m_pressed)
        return;
    setPressed(false);
}

/* ---- led ---- */

LedWidget::LedWidget(const QString &name, QWidget *parent)
    : QWidget(parent), m_name(name)
{
    setToolTip(name);
    setMinimumSize(sizeHint());
}

QSize LedWidget::sizeHint() const
{
    return QSize(60, 44);
}

void LedWidget::setLed(const QColor &color)
{
    m_led = color;
    update();
}

void LedWidget::setSkinned(bool skinned)
{
    m_skinned = skinned;
    setMinimumSize(skinned ? QSize(4, 4) : sizeHint());
}

void LedWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* skinned: the picture draws the led off, a lit one is drawn over it */
    if (m_skinned)
    {
        if (m_led.red() + m_led.green() + m_led.blue() < 8)
            return;
        const QColor c = ledLook(m_led);
        const QPointF center(width() / 2.0, height() / 2.0);
        const double r = std::min(width(), height()) / 2.0;
        QRadialGradient g(center, r);
        g.setColorAt(0, c.lighter(130));
        g.setColorAt(0.75, c);
        g.setColorAt(1, c.darker(160));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(center, r, r);
        return;
    }

    p.setPen(QPen(QColor(10, 10, 10), 1));
    p.setBrush(ledLook(ledOrOff(m_led)));
    p.drawEllipse(QRectF(width() / 2.0 - 8, 4, 16, 16));
    drawLabel(p, rect(), m_name);
}

/* ---- panel picture ---- */

PanelWidget::PanelWidget(const QPixmap &picture, QWidget *parent)
    : QWidget(parent), m_picture(picture)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(m_picture.size() / 4);
}

QSize PanelWidget::sizeHint() const
{
    return m_picture.size();
}

void PanelWidget::place(QWidget *w, const QRect &rect)
{
    m_placed.emplace_back(w, rect);
    layoutChildren();
}

/* where the picture goes: as large as fits, aspect kept, centred */
QRectF PanelWidget::target() const
{
    const QSizeF s = QSizeF(m_picture.size()).scaled(size(), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0), s);
}

void PanelWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(target(), m_picture, QRectF(m_picture.rect()));
}

void PanelWidget::resizeEvent(QResizeEvent *)
{
    layoutChildren();
}

void PanelWidget::layoutChildren()
{
    const QRectF t = target();
    const double k = t.width() / m_picture.width();
    for (const auto &placed : m_placed)
    {
        const QRect &r = placed.second;
        placed.first->setGeometry(QRectF(t.x() + r.x() * k, t.y() + r.y() * k, r.width() * k, r.height() * k)
                                      .toAlignedRect());
    }
}
