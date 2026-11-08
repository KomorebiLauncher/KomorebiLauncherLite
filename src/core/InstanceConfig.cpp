// SPDX-License-Identifier: GPL-3.0-only
#include "core/InstanceConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Komorebi {

namespace {

QString configPath(const QString& versionDirectory)
{
    return QDir(versionDirectory).filePath(QStringLiteral("instance.json"));
}

}  // namespace

InstanceConfig InstanceConfig::load(const QString& versionDirectory)
{
    InstanceConfig config;

    QFile file(configPath(versionDirectory));
    if (!file.open(QIODevice::ReadOnly))
        return config;

    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    config.displayName = root.value(QStringLiteral("displayName")).toString();
    config.description = root.value(QStringLiteral("description")).toString();
    config.launchCount = root.value(QStringLiteral("launchCount")).toInt(0);
    config.lastLaunched = root.value(QStringLiteral("lastLaunched")).toString();
    config.memoryMb = root.value(QStringLiteral("memoryMb")).toInt(0);
    config.jvmArguments = root.value(QStringLiteral("jvmArguments")).toString();
    config.gameArguments = root.value(QStringLiteral("gameArguments")).toString();
    return config;
}

void InstanceConfig::save(const QString& versionDirectory) const
{
    QJsonObject root;
    root.insert(QStringLiteral("displayName"), displayName);
    root.insert(QStringLiteral("description"), description);
    root.insert(QStringLiteral("launchCount"), launchCount);
    root.insert(QStringLiteral("lastLaunched"), lastLaunched);
    root.insert(QStringLiteral("memoryMb"), memoryMb);
    root.insert(QStringLiteral("jvmArguments"), jvmArguments);
    root.insert(QStringLiteral("gameArguments"), gameArguments);

    const QString path = configPath(versionDirectory);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

}  // namespace Komorebi
