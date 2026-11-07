// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The kit: every control paints itself from the application palette, so its look never depends on
 *  which platform style happened to be active and never on the widget's own palette (which the
 *  stylesheet can leave transparent, and a transparent window colour reads as black).
 */

#pragma once

#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSlider>

class QVBoxLayout;
class QFocusEvent;

namespace Komorebi {

namespace Radius {
constexpr int Chip = 2;
constexpr int Control = 4;
constexpr int Surface = 6;
}  // namespace Radius

/// The colours a control needs, taken from the application palette the theme filled.
struct Colours {
    QColor paper;
    QColor paperDeep;
    QColor ink;
    QColor inkSoft;
    QColor plum;
    QColor line;

    static Colours current();

    QColor wash(int alpha) const { return QColor(plum.red(), plum.green(), plum.blue(), alpha); }
};

/// Draws a rounded plate and returns the painter for the caller's own marks.
void paintPlate(QPainter& painter, const QRectF& rect, int radius, const QColor& fill, const QColor& border);

class Button : public QPushButton {
    Q_OBJECT

   public:
    explicit Button(const QString& text, QWidget* parent = nullptr);
    void setPrimary(bool primary);
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    bool m_primary = false;
    bool m_hovered = false;
};

class Field : public QLineEdit {
    Q_OBJECT

   public:
    explicit Field(QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent* event) override;
};

class Combo : public QComboBox {
    Q_OBJECT

   public:
    explicit Combo(QWidget* parent = nullptr);
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    bool m_keyboardFocus = false;
};

class Slider : public QSlider {
    Q_OBJECT

   public:
    explicit Slider(Qt::Orientation orientation, QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent* event) override;
};

class Card : public QFrame {
    Q_OBJECT

   public:
    explicit Card(QWidget* parent = nullptr);
    QVBoxLayout* body() const { return m_body; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QVBoxLayout* m_body = nullptr;
};

}  // namespace Komorebi
