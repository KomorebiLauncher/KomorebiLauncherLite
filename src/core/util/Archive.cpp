// SPDX-License-Identifier: GPL-3.0-only
#include "core/util/Archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace Komorebi {

namespace {

/// tar.exe ships with Windows 10 1803 and later; its -xf reads zip as well as tar.
QString tarPath()
{
#if defined(Q_OS_WIN)
    const QString system = QDir(QStringLiteral("C:/Windows/System32")).filePath(QStringLiteral("tar.exe"));
    if (QFileInfo::exists(system))
        return system;
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
#else
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
#endif
}

}  // namespace

bool looksLikeArchive(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray magic = file.read(4);
    // "PK\u0003\u0004" is a zip; the empty-archive variant starts the same way.
    return magic.startsWith("PK");
}

bool extractArchive(const QString& archive, const QString& targetDirectory, QString* error, const QStringList& only)
{
    if (!QFileInfo::exists(archive)) {
        *error = QStringLiteral("%1 does not exist").arg(archive);
        return false;
    }

    const QString tar = tarPath();
    if (tar.isEmpty()) {
        *error = QStringLiteral("no tar to unpack %1 with").arg(QFileInfo(archive).fileName());
        return false;
    }

    QDir().mkpath(targetDirectory);

    QStringList arguments{ QStringLiteral("-xf"), QDir::toNativeSeparators(archive), QStringLiteral("-C"),
                           QDir::toNativeSeparators(targetDirectory) };
    arguments += only;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tar, arguments);
    if (!process.waitForStarted(5000)) {
        *error = QStringLiteral("tar did not start");
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        *error = QStringLiteral("tar took too long on %1").arg(QFileInfo(archive).fileName());
        return false;
    }
    if (process.exitCode() != 0) {
        *error = QString::fromLocal8Bit(process.readAll()).trimmed();
        if (error->isEmpty())
            *error = QStringLiteral("tar failed on %1").arg(QFileInfo(archive).fileName());
        return false;
    }
    return true;
}

}  // namespace Komorebi
