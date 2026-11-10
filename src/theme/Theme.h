// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The theme: one manifest of colours and metrics, one stylesheet that refers to it by name.
 *  Loading a theme means reading both, expanding the references, and handing the result to Qt.
 */

#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>

class QApplication;

namespace Komorebi {

struct Theme {
    QString id;
    QString name;
    QHash<QString, QString> tokens;
    QString sheet;
    QImage wallpaper;

    QColor colour(const QString& token) const;

    /// Reads `themeDir/theme.json` and the sheet it names, falling back to the copy compiled into
    /// the binary when the folder is not there; returns an empty theme only when neither has one.
    static Theme load(const QString& themeDir);

    /// Sets the palette and stylesheet the theme describes.
    void apply(QApplication& app) const;
};

}  // namespace Komorebi
