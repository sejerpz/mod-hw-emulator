/*
 * mod-hw-emulator — panel controls
 */

#include "controls.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

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
    p.fillRect(rect(), kPanel);

    const int scale = std::max(1, std::min((width() - 24) / m_width, (height() - 24) / m_height));
    const int w = m_width * scale, h = m_height * scale;
    const QRect screen((width() - w) / 2, (height() - h) / 2, w, h);

    /* bezel */
    p.fillRect(screen.adjusted(-10, -10, 10, 10), QColor(12, 12, 14));

    /* negative LCD: lit pixels show the backlight, the rest stays dark */
    const double k = 0.25 + 0.75 * m_brightness / 255.0;
    const QColor lit(static_cast<int>(235 * k), static_cast<int>(240 * k), static_cast<int>(245 * k));
    const QColor off(14, 15, 18);
    p.fillRect(screen, off);

    const int stride = (m_width + 7) / 8;
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

void EncoderWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

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

    /* pointer: 20 detents per turn */
    const double a = (m_angle % 20) * (2 * M_PI / 20) - M_PI / 2;
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

void SwitchWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

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

    drawLabel(p, rect(), m_name);
}

void SwitchWidget::mousePressEvent(QMouseEvent *)
{
    setPressed(true);
}

void SwitchWidget::mouseReleaseEvent(QMouseEvent *)
{
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

void LedWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(10, 10, 10), 1));
    p.setBrush(ledLook(ledOrOff(m_led)));
    p.drawEllipse(QRectF(width() / 2.0 - 8, 4, 16, 16));
    drawLabel(p, rect(), m_name);
}
