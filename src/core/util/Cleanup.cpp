// SPDX-License-Identifier: GPL-3.0-only
#include "core/util/Cleanup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Komorebi {

namespace {

qint64 sizeOf(const QString& path)
{
    qint64 total = 0;
    const QDir directory(path);
    if (!directory.exists())
        return 0;
    for (const auto& entry : directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        total += entry.isDir() ? sizeOf(entry.absoluteFilePath()) : entry.size();
    return total;
}

qint64 wipe(const QString& path)
{
    const QDir directory(path);
    if (!directory.exists())
        return 0;

    qint64 freed = 0;
    for (const auto& entry : directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = entry.absoluteFilePath();
        freed += entry.isDir() ? sizeOf(target) : entry.size();
        if (entry.isDir())
            QDir(target).removeRecursively();
        else
            QFile::remove(target);
    }
    return freed;
}

}  // namespace

qint64 cleanJunk(const QString& dataRoot)
{
    qint64 freed = 0;

    freed += wipe(QDir(dataRoot).filePath(QStringLiteral("logs")));

    // Every version keeps its own logs next to the game; the game's own crash reports too.
    const QDir versions(QDir(dataRoot).filePath(QStringLiteral("versions")));
    for (const auto& version : versions.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir base(versions.filePath(version));
        freed += wipe(base.filePath(QStringLiteral("logs")));
        freed += wipe(base.filePath(QStringLiteral("crash-reports")));
        freed += wipe(base.filePath(QStringLiteral("tmp")));
    }
    return freed;
}

}  // namespace Komorebi
