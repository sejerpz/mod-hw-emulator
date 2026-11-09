/*
 * mod-hw-emulator — panel controls
 *
 * Each control is a widget bound to a logical id of the device. Controls know nothing of where they are:
 * a layout places them (SimpleLayout, or SkinLayout on a picture of the device).
 *
 * Skinned, a control lies over its own picture on the panel and draws only what changes: the pointer of a
 * knob, the colour of a led, a pressed or armed switch. The picture shows through the rest.
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <functional>
#include <utility>
#include <vector>

/* the LCD: the frame scaled up, with the pixel grid and the backlight */
class LcdWidget : public QWidget
{
public:
    explicit LcdWidget(int width, int height, QWidget *parent = nullptr);

    void setFrame(int width, int height, const std::vector<uint8_t> &mono1, int brightness);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void setSkinned(bool skinned) { m_skinned = skinned; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_skinned = false;
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
    void setSkinned(bool skinned);

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    bool m_skinned = false;
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
    bool isPressed() const { return m_pressed; }

    /* armed with Ctrl+click: the next click on another switch presses both at once */
    bool isArmed() const { return m_armed; }
    void setArmed(bool armed);

    QSize sizeHint() const override;
    void setSkinned(bool skinned);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    bool m_skinned = false;
    QString m_name;
    bool m_footswitch;
    QColor m_led = Qt::black;
    bool m_hasLed = false;
    bool m_pressed = false;
    bool m_armed = false;
};

/* a led with no control of its own */
class LedWidget : public QWidget
{
public:
    LedWidget(const QString &name, QWidget *parent = nullptr);
    void setLed(const QColor &color);
    QSize sizeHint() const override;
    void setSkinned(bool skinned);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_skinned = false;
    QString m_name;
    QColor m_led = Qt::black;
};

/* the picture of the device: lays its children on their rectangles, at the scale it is drawn */
class PanelWidget : public QWidget
{
public:
    PanelWidget(const QPixmap &picture, QWidget *parent = nullptr);

    /* RECT in the pixels of the picture */
    void place(QWidget *w, const QRect &rect);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QRectF target() const;
    void layoutChildren();

    QPixmap m_picture;
    std::vector<std::pair<QWidget *, QRect>> m_placed;
};
