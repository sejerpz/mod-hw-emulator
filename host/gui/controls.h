/*
 * mod-hw-emulator — panel controls
 *
 * Each control is a widget bound to a logical id of the device. Controls know nothing of where they are:
 * a layout places them (SimpleLayout now; a skin layout will put them on a picture of the device).
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

#include <functional>
#include <vector>

/* the LCD: the frame scaled up, with the pixel grid and the backlight */
class LcdWidget : public QWidget
{
public:
    explicit LcdWidget(int width, int height, QWidget *parent = nullptr);

    void setFrame(int width, int height, const std::vector<uint8_t> &mono1, int brightness);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_width, m_height;
    std::vector<uint8_t> m_bits;
    int m_brightness = 255;
};

/* an endless encoder with push: wheel or vertical drag turns it, a click pushes it */
class EncoderWidget : public QWidget
{
public:
    EncoderWidget(const QString &name, QWidget *parent = nullptr);

    std::function<void(int steps)> onTurn;
    std::function<void(bool pressed)> onPress;

    void setLed(const QColor &color);
    void turnFromKeyboard(int steps);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QString m_name;
    QColor m_led = Qt::black;
    int m_angle = 0;          /* in steps, for the drawing */
    int m_dragY = 0;
    int m_dragged = 0;
    bool m_pressed = false;
    int m_wheelRest = 0;
};

/* a footswitch or a button, with its led */
class SwitchWidget : public QWidget
{
public:
    SwitchWidget(const QString &name, bool footswitch, QWidget *parent = nullptr);

    std::function<void(bool pressed)> onPress;

    void setLed(const QColor &color);
    void setPressed(bool pressed);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QString m_name;
    bool m_footswitch;
    QColor m_led = Qt::black;
    bool m_hasLed = false;
    bool m_pressed = false;
};

/* a led with no control of its own */
class LedWidget : public QWidget
{
public:
    LedWidget(const QString &name, QWidget *parent = nullptr);
    void setLed(const QColor &color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_name;
    QColor m_led = Qt::black;
};
