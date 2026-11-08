// SPDX-License-Identifier: GPL-3.0-only
#include "theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>

namespace Komorebi {

QColor Theme::colour(const QString& token) const
{
    return QColor(tokens.value(token));
}

Theme Theme::load(const QString& themeDir)
{
    Theme theme;
    theme.id = QDir(themeDir).dirName();

    QFile manifest(QDir(themeDir).filePath(QStringLiteral("theme.json")));
    if (!manifest.open(QIODevice::ReadOnly))
        return theme;

    const auto root = QJsonDocument::fromJson(manifest.readAll()).object();
    theme.name = root.value(QStringLiteral("name")).toString(theme.id);

    const auto tokens = root.value(QStringLiteral("tokens")).toObject();
    for (auto it = tokens.begin(); it != tokens.end(); ++it)
        theme.tokens.insert(it.key(), it.value().toString());

    // Only the file name: a theme is a directory of things, and a manifest does not get to point
    // the launcher at an arbitrary file to load as its stylesheet.
    const QString sheetName = QFileInfo(root.value(QStringLiteral("qssFilePath")).toString(QStringLiteral("theme.css"))).fileName();
    QFile sheet(QDir(themeDir).filePath(sheetName));
    if (sheet.open(QIODevice::ReadOnly)) {
        static const QRegularExpression reference(QStringLiteral("@([A-Za-z][A-Za-z0-9_]*)"));
        QString text = QString::fromUtf8(sheet.readAll());

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

    const QImage wallpaper(QDir(themeDir).filePath(QStringLiteral("background.png")));
    theme.wallpaper = wallpaper;

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
