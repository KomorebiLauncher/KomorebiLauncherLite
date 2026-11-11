// SPDX-License-Identifier: GPL-3.0-only
#include "kit/Kit.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QGuiApplication>
#include <QScreen>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainterPath>
#include <QVBoxLayout>

namespace Komorebi {

static QColor mix(const QColor& a, const QColor& b, double t)
{
    const auto lerp = [t](int x, int y) { return int(x + (y - x) * t); };
    return QColor(lerp(a.red(), b.red()), lerp(a.green(), b.green()), lerp(a.blue(), b.blue()));
}

Colours Colours::current()
{
    const QPalette palette = QApplication::palette();
    Colours colours;
    colours.paper = palette.color(QPalette::Window);
    colours.paperDeep = palette.color(QPalette::Button);
    colours.ink = palette.color(QPalette::WindowText);
    colours.plum = palette.color(QPalette::Link);
    colours.inkSoft = mix(colours.ink, colours.paper, 0.45);
    colours.line = mix(colours.ink, colours.paper, 0.78);
    return colours;
}

void paintPlate(QPainter& painter, const QRectF& rect, int radius, const QColor& fill, const QColor& border)
{
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    if (fill.alpha() > 0)
        painter.fillPath(path, fill);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

// ---- Button -------------------------------------------------------------------------------

Button::Button(const QString& text, QWidget* parent) : QPushButton(text, parent)
{
    setCursor(Qt::PointingHandCursor);
}

void Button::setPrimary(bool primary)
{
    m_primary = primary;
    update();
}

QSize Button::sizeHint() const
{
    const QFontMetrics metrics = fontMetrics();
    return QSize(metrics.horizontalAdvance(text()) + 36, qMax(32, metrics.height() + 16));
}

void Button::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QPushButton::enterEvent(event);
}

void Button::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QPushButton::leaveEvent(event);
}

void Button::paintEvent(QPaintEvent*)
{
    const Colours colours = Colours::current();

    QColor fill = Qt::transparent;
    QColor border = m_primary ? colours.plum : colours.line;
    QColor label = m_primary ? colours.paper : colours.ink;

    if (m_primary) {
        fill = colours.plum;
        if (m_hovered)
            fill = colours.plum.lighter(112);
    } else if (m_hovered) {
        fill = colours.wash(26);
    }
    if (!isEnabled()) {
        fill = Qt::transparent;
        border = colours.line;
        label = colours.inkSoft;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintPlate(painter, QRectF(rect()), Radius::Control, fill, border);
    painter.setPen(label);
    painter.setFont(font());
    painter.drawText(rect(), Qt::AlignCenter, text());
}

// ---- Field --------------------------------------------------------------------------------

Field::Field(QWidget* parent) : QLineEdit(parent)
{
    setFrame(false);
    setTextMargins(11, 0, 11, 0);
    // A line edit fills its own background with the Base colour and the theme makes Base the same
    // paper the window is painted with, so the style painted over the plate this class draws and every
    // input in the launcher came out with no frame at all. Handing the background to the stylesheet's
    // transparent keeps the drawing in paintEvent, where the plate and its focus border live.
    setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0;"));
}

void Field::paintEvent(QPaintEvent* event)
{
    const Colours colours = Colours::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintPlate(painter, QRectF(rect()), Radius::Control, isEnabled() ? colours.paper : colours.paperDeep,
               hasFocus() ? colours.plum : colours.line);
    painter.end();
    QLineEdit::paintEvent(event);
}

// ---- Combo --------------------------------------------------------------------------------

Combo::Combo(QWidget* parent) : QComboBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

/**
 * Shows the popup, but not at the height Qt would like.
 *
 * With a stylesheet on the view -- which every control here has -- Qt stops honouring maxVisibleItems
 * and sizes the popup from the whole list, so picking a version out of a hundred drops a column taller
 * than the screen over everything. The popup is capped to a readable number of rows here instead.
 *
 * The cap is applied after Qt has already placed the popup, and the popup it placed was taller than
 * the screen: it went above the combo and ended up at the top of the screen, where a shrink left it.
 * Only the next open looked right, because the maximum height had stuck to the reused popup. So the
 * popup is placed again here, now that it fits.
 */
void Combo::showPopup()
{
    QComboBox::showPopup();

    QAbstractItemView* popupView = view();
    if (!popupView)
        return;
    QWidget* container = popupView->window();
    if (!container || container == this)
        return;

    constexpr int rowsVisible = 12;
    const int rowHeight = qMax(1, popupView->sizeHintForRow(0));
    const int border = container->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, container) * 2;
    const int wanted = rowHeight * qMin(qMax(count(), 1), rowsVisible) + border + 4;
    container->setMaximumHeight(wanted);

    const QSize popupSize(container->width(), qMin(container->height(), wanted));
    container->resize(popupSize);

    // Below the combo when it fits, above it otherwise, and never off the screen the combo is on.
    const QRect comboRect(mapToGlobal(QPoint(0, 0)), size());
    const QScreen* screen = QGuiApplication::screenAt(comboRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : comboRect;

    int x = qBound(area.left(), comboRect.left(),
                   qMax(area.left(), area.right() - popupSize.width() + 1));
    int y = comboRect.bottom() + 1;
    if (y + popupSize.height() > area.bottom())
        y = comboRect.top() - popupSize.height();
    y = qBound(area.top(), y, qMax(area.top(), area.bottom() - popupSize.height() + 1));
    container->move(x, y);
}

QSize Combo::sizeHint() const
{
    const QSize base = QComboBox::sizeHint();
    return QSize(base.width() + 32, qMax(32, base.height() + 8));
}

void Combo::focusInEvent(QFocusEvent* event)
{
    switch (event->reason()) {
        case Qt::TabFocusReason:
        case Qt::BacktabFocusReason:
        case Qt::ShortcutFocusReason:
            m_keyboardFocus = true;
            break;
        default:
            m_keyboardFocus = false;
            break;
    }
    QComboBox::focusInEvent(event);
    update();
}

void Combo::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QComboBox::focusOutEvent(event);
    update();
}

void Combo::paintEvent(QPaintEvent*)
{
    const Colours colours = Colours::current();
    const bool open = view() && view()->isVisible();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintPlate(painter, QRectF(rect()), Radius::Control, colours.paper,
               open || (m_keyboardFocus && hasFocus()) ? colours.plum : colours.line);

    const QRect textRect = rect().adjusted(12, 0, -30, 0);
    painter.setPen(colours.ink);
    painter.setFont(font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     fontMetrics().elidedText(currentText(), Qt::ElideRight, textRect.width()));

    const QPoint at(rect().right() - 16, rect().center().y());
    QPainterPath chevron;
    const int half = 4, lift = 2;
    if (open) {
        chevron.moveTo(at.x() - half, at.y() + lift);
        chevron.lineTo(at.x(), at.y() - lift);
        chevron.lineTo(at.x() + half, at.y() + lift);
    } else {
        chevron.moveTo(at.x() - half, at.y() - lift);
        chevron.lineTo(at.x(), at.y() + lift);
        chevron.lineTo(at.x() + half, at.y() - lift);
    }
    painter.setPen(QPen(colours.ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(chevron);
}

// ---- Slider -------------------------------------------------------------------------------

Slider::Slider(Qt::Orientation orientation, QWidget* parent) : QSlider(orientation, parent)
{
    setCursor(Qt::PointingHandCursor);
}

void Slider::paintEvent(QPaintEvent*)
{
    const Colours colours = Colours::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool horizontal = orientation() == Qt::Horizontal;
    const int length = horizontal ? width() : height();
    const int centre = horizontal ? height() / 2 : width() / 2;
    const int handle = 14;
    const int span = qMax(1, length - handle);
    const int range = maximum() - minimum();
    const double position = range > 0 ? double(value() - minimum()) / double(range) : 0.0;
    const int travel = int(position * span) + handle / 2;

    const QRect groove = horizontal ? QRect(0, centre - 2, length, 4) : QRect(centre - 2, 0, 4, length);
    const QRect filled = horizontal ? QRect(0, groove.top(), travel, 4) : QRect(groove.left(), groove.top(), 4, travel);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.line);
    painter.drawRoundedRect(groove, 2, 2);
    painter.setBrush(colours.plum);
    painter.drawRoundedRect(filled, 2, 2);

    const QPointF centreOfHandle = horizontal ? QPointF(travel, centre) : QPointF(centre, travel);
    painter.setBrush(colours.paper);
    painter.setPen(QPen(colours.plum, 2.0));
    painter.drawEllipse(centreOfHandle, handle / 2.0, handle / 2.0);
}

// ---- Card ---------------------------------------------------------------------------------

Card::Card(QWidget* parent) : QFrame(parent)
{
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(16, 14, 16, 14);
    m_body->setSpacing(12);
}

void Card::paintEvent(QPaintEvent*)
{
    const Colours colours = Colours::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintPlate(painter, QRectF(rect()), Radius::Surface, QColor(colours.paper.red(), colours.paper.green(), colours.paper.blue(), 190),
               colours.line);
}

}  // namespace Komorebi
