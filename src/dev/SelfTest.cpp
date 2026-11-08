// SPDX-License-Identifier: GPL-3.0-only
#include "dev/SelfTest.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "kit/Kit.h"

namespace Komorebi::Dev {

namespace {

int g_failures = 0;
QStringList g_report;

int luminance(const QColor& colour)
{
    return (colour.red() * 299 + colour.green() * 587 + colour.blue() * 114) / 1000;
}

void report(bool ok, const QString& what, const QString& detail)
{
    if (!ok)
        ++g_failures;
    const QString line = QStringLiteral("%1  %2%3").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"),
                                                       what, detail.isEmpty() ? QString() : QStringLiteral(" — ") + detail);
    g_report << line;
}

/// Renders a widget off-screen and hands back its pixels.
QImage render(QWidget* widget, QSize size)
{
    widget->resize(size);
    widget->move(-4000, -4000);
    widget->show();
    QApplication::processEvents();
    const QImage image = widget->grab().toImage();
    image.save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("selftest-%1.png").arg(QString::fromLatin1(widget->metaObject()->className()))));
    widget->hide();
    return image;
}

}  // namespace

int runSelfTest()
{
    g_failures = 0;
    const QPalette palette = QApplication::palette();
    const QColor paper = palette.color(QPalette::Window);
    const QColor ink = palette.color(QPalette::WindowText);
    const QColor plum = palette.color(QPalette::Link);

    g_report << QStringLiteral("rendering self-test");

    // 1. the palette itself: a launcher whose window colour is dark is already broken
    report(luminance(paper) > 200, "window colour is paper", paper.name());
    report(luminance(ink) < 120, "text colour is ink", ink.name());

    // 2. a plain window: whatever paints the surface must not leave black behind
    {
        QWidget surface;
        const QImage image = render(&surface, QSize(200, 120));
        report(luminance(image.pixelColor(100, 60)) > 200, "empty surface is paper",
               image.pixelColor(100, 60).name());
    }

    // 3. the kit's buttons: the secondary one is paper, the primary one is the accent
    {
        Komorebi::Button secondary(QStringLiteral("cancel"));
        const QImage image = render(&secondary, QSize(140, 34));
        report(luminance(image.pixelColor(12, 17)) > 180, "button surface is paper",
               image.pixelColor(12, 17).name());

        Komorebi::Button primary(QStringLiteral("launch"));
        primary.setPrimary(true);
        const QImage imagePrimary = render(&primary, QSize(140, 34));
        report(imagePrimary.pixelColor(12, 17) == plum, "primary button is plum",
               imagePrimary.pixelColor(12, 17).name());
    }

    // 4. the field: paper with a visible edge, and the text is the ink colour
    {
        Komorebi::Field field;
        field.setText(QStringLiteral("1.20.1"));
        const QImage image = render(&field, QSize(200, 34));
        report(luminance(image.pixelColor(100, 6)) > 180, "field surface is paper",
               image.pixelColor(100, 6).name());
        const QColor edge = image.pixelColor(0, 17);
        report(edge != QColor(Qt::black), "field edge is drawn", edge.name());
    }

    // 5. the combo: its own label is painted, so the middle of the row is not empty paper
    {
        Komorebi::Combo combo;
        combo.addItems({ QStringLiteral("sidebar"), QStringLiteral("rail") });
        const QImage image = render(&combo, QSize(220, 34));
        bool foundInk = false;
        for (int x = 12; x < 120 && !foundInk; ++x)
            for (int y = 8; y < 26 && !foundInk; ++y)
                if (image.pixelColor(x, y) == ink)
                    foundInk = true;
        report(foundInk, "combo paints its label", QString());
    }

    // 6. a dialog: the surface the old code kept getting wrong
    {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("…"), &dialog));
        const QImage image = render(&dialog, QSize(240, 140));
        report(luminance(image.pixelColor(120, 100)) > 180, "dialog surface is paper",
               image.pixelColor(120, 100).name());
    }

    // 7. a popup menu: a top-level window of its own, so it does not inherit the above
    {
        QMenu menu;
        menu.addAction(QStringLiteral("rename"));
        const QImage image = render(&menu, QSize(160, 80));
        report(luminance(image.pixelColor(80, 40)) > 180, "menu surface is paper",
               image.pixelColor(80, 40).name());
    }

    g_report << (g_failures == 0 ? QStringLiteral("self-test passed") : QStringLiteral("self-test failed: %1").arg(g_failures));

    QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("selftest.log")));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(g_report.join(QLatin1Char('\n')).toUtf8());
    return g_failures;
}

}  // namespace Komorebi::Dev
