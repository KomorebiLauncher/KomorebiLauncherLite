// SPDX-License-Identifier: GPL-3.0-only
#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/Paths.h"

namespace Komorebi {

namespace {

QString settingsPath()
{
    return QDir(dataRoot()).filePath(QStringLiteral("settings.json"));
}

}  // namespace

Settings Settings::load()
{
    Settings settings;

    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly))
        return settings;

    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const QString language = root.value(QStringLiteral("language")).toString();
    if (!language.isEmpty())
        settings.language = language;

    const int memory = root.value(QStringLiteral("memoryMb")).toInt();
    if (memory >= 512)
        settings.memoryMb = memory;

    settings.javaPath = root.value(QStringLiteral("javaPath")).toString();
    return settings;
}

void Settings::save() const
{
    QJsonObject root;
    root.insert(QStringLiteral("language"), language);
    root.insert(QStringLiteral("memoryMb"), memoryMb);
    root.insert(QStringLiteral("javaPath"), javaPath);

    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

}  // namespace Komorebi
