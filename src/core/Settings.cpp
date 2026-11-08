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

    settings.darkTheme = root.value(QStringLiteral("darkTheme")).toBool(false);
    settings.wallpaperOpacity = root.value(QStringLiteral("wallpaperOpacity")).toInt(100);
    if (settings.wallpaperOpacity < 0 || settings.wallpaperOpacity > 100)
        settings.wallpaperOpacity = 100;

    const int memory = root.value(QStringLiteral("memoryMb")).toInt();
    if (memory >= 512)
        settings.memoryMb = memory;

    settings.msaClientId = root.value(QStringLiteral("msaClientId")).toString();
    settings.downloadSite = root.value(QStringLiteral("downloadSite")).toString(QStringLiteral("official"));
    settings.maxConcurrency = qMax(1, root.value(QStringLiteral("maxConcurrency")).toInt(8));
    settings.speedLimitKb = qMax(0, root.value(QStringLiteral("speedLimitKb")).toInt(0));

    settings.lockMemory = root.value(QStringLiteral("lockMemory")).toBool(false);
    settings.width = root.value(QStringLiteral("width")).toInt(0);
    settings.height = root.value(QStringLiteral("height")).toInt(0);
    if (settings.width < 0 || settings.height < 0) {
        settings.width = 0;
        settings.height = 0;
    }

    settings.javaPath = root.value(QStringLiteral("javaPath")).toString();
    settings.jvmArguments = root.value(QStringLiteral("jvmArguments")).toString();
    settings.gameArguments = root.value(QStringLiteral("gameArguments")).toString();
    settings.preLaunchCommand = root.value(QStringLiteral("preLaunchCommand")).toString();
    settings.highPerformanceGpu = root.value(QStringLiteral("highPerformanceGpu")).toBool(false);
    settings.legacyFix = root.value(QStringLiteral("legacyFix")).toBool(false);
    settings.unsafeAgent = root.value(QStringLiteral("unsafeAgent")).toBool(true);
    settings.processPriority = root.value(QStringLiteral("processPriority")).toString();
    return settings;
}

void Settings::save() const
{
    QJsonObject root;
    root.insert(QStringLiteral("language"), language);
    root.insert(QStringLiteral("darkTheme"), darkTheme);
    root.insert(QStringLiteral("wallpaperOpacity"), wallpaperOpacity);
    root.insert(QStringLiteral("memoryMb"), memoryMb);
    root.insert(QStringLiteral("msaClientId"), msaClientId);
    root.insert(QStringLiteral("downloadSite"), downloadSite);
    root.insert(QStringLiteral("maxConcurrency"), maxConcurrency);
    root.insert(QStringLiteral("speedLimitKb"), speedLimitKb);
    root.insert(QStringLiteral("lockMemory"), lockMemory);
    root.insert(QStringLiteral("width"), width);
    root.insert(QStringLiteral("height"), height);
    root.insert(QStringLiteral("javaPath"), javaPath);
    root.insert(QStringLiteral("jvmArguments"), jvmArguments);
    root.insert(QStringLiteral("gameArguments"), gameArguments);
    root.insert(QStringLiteral("preLaunchCommand"), preLaunchCommand);
    root.insert(QStringLiteral("highPerformanceGpu"), highPerformanceGpu);
    root.insert(QStringLiteral("legacyFix"), legacyFix);
    root.insert(QStringLiteral("unsafeAgent"), unsafeAgent);
    root.insert(QStringLiteral("processPriority"), processPriority);

    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

QStringList Settings::splitArguments(const QString& text)
{
    // Space separated, with "quotes" grouping: the same rule the operating system uses.
    QStringList arguments;
    QString current;
    bool quoted = false;
    for (const QChar character : text) {
        if (character == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (character.isSpace() && !quoted) {
            if (!current.isEmpty()) {
                arguments.append(current);
                current.clear();
            }
            continue;
        }
        current.append(character);
    }
    if (!current.isEmpty())
        arguments.append(current);
    return arguments;
}

}  // namespace Komorebi
