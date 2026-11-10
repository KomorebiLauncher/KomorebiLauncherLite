// SPDX-License-Identifier: GPL-3.0-only
#include "theme/Theme.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>

namespace Komorebi {

namespace {

/// One file of a theme: from the folder when it is there, from the compiled-in copy otherwise.
///
/// The folder wins, because a theme is something a player may edit; the copy is what the single-file
/// build has, and without it that build opened with no palette, no sheet and no wallpaper at all --
/// a black window with unreadable text on it.
QByteArray themeFile(const QString& themeDir, const QString& name)
{
    QFile onDisk(QDir(themeDir).filePath(name));
    if (onDisk.open(QIODevice::ReadOnly))
        return onDisk.readAll();

    QFile builtIn(QStringLiteral(":/theme/%1/%2").arg(QFileInfo(themeDir).fileName(), name));
    if (builtIn.open(QIODevice::ReadOnly))
        return builtIn.readAll();

    return {};
}

}  // namespace

QColor Theme::colour(const QString& token) const
{
    return QColor(tokens.value(token));
}

Theme Theme::load(const QString& themeDir)
{
    Theme theme;
    theme.id = QDir(themeDir).dirName();

    const QByteArray manifest = themeFile(themeDir, QStringLiteral("theme.json"));
    if (manifest.isEmpty())
        return theme;

    const auto root = QJsonDocument::fromJson(manifest).object();
    theme.name = root.value(QStringLiteral("name")).toString(theme.id);

    const auto tokens = root.value(QStringLiteral("tokens")).toObject();
    for (auto it = tokens.begin(); it != tokens.end(); ++it)
        theme.tokens.insert(it.key(), it.value().toString());

    // Only the file name: a theme is a directory of things, and a manifest does not get to point
    // the launcher at an arbitrary file to load as its stylesheet.
    const QString sheetName = QFileInfo(root.value(QStringLiteral("qssFilePath")).toString(QStringLiteral("theme.css"))).fileName();
    const QByteArray sheetData = themeFile(themeDir, sheetName);
    if (!sheetData.isEmpty()) {
        static const QRegularExpression reference(QStringLiteral("@([A-Za-z][A-Za-z0-9_]*)"));
        const QString text = QString::fromUtf8(sheetData);

        QString expanded;
        int last = 0;
        auto matches = reference.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            expanded += text.mid(last, match.capturedStart() - last);
            expanded += theme.tokens.value(match.captured(1), match.captured(0));
            last = match.capturedEnd();
        }
        expanded += text.mid(last);
        theme.sheet = expanded;
    }

    const QByteArray background = themeFile(themeDir, QStringLiteral("background.png"));
    if (!background.isEmpty())
        theme.wallpaper = QImage::fromData(background);

    return theme;
}

void Theme::apply(QApplication& app) const
{
    // The widget style is the base everything is drawn on; Fusion behaves the same everywhere,
    // which is what the kit's painting assumes.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, colour(QStringLiteral("paper")));
    palette.setColor(QPalette::WindowText, colour(QStringLiteral("ink")));
    palette.setColor(QPalette::Base, colour(QStringLiteral("paper")));
    palette.setColor(QPalette::AlternateBase, colour(QStringLiteral("paperDeep")));
    palette.setColor(QPalette::Text, colour(QStringLiteral("ink")));
    palette.setColor(QPalette::Button, colour(QStringLiteral("paperDeep")));
    palette.setColor(QPalette::ButtonText, colour(QStringLiteral("ink")));
    palette.setColor(QPalette::Highlight, colour(QStringLiteral("plum")));
    palette.setColor(QPalette::HighlightedText, colour(QStringLiteral("paper")));
    palette.setColor(QPalette::ToolTipBase, colour(QStringLiteral("paper")));
    palette.setColor(QPalette::ToolTipText, colour(QStringLiteral("ink")));
    palette.setColor(QPalette::Link, colour(QStringLiteral("plum")));
    app.setPalette(palette);

    app.setStyleSheet(sheet);
}

}  // namespace Komorebi
