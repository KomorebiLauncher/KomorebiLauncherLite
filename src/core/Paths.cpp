// SPDX-License-Identifier: GPL-3.0-only
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

namespace Komorebi {

namespace {

/// The remembered data directory, or an empty string when there is none worth using.
QString readDataRootPointer()
{
    QFile file(dataRootPointer());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString line = QString::fromUtf8(file.readAll()).trimmed();
    // A relative path in here would depend on the working directory the launcher was started from,
    // which is not something a player can be expected to control.
    if (line.isEmpty() || !QDir::isAbsolutePath(line))
        return {};
    return QDir::cleanPath(line);
}

}  // namespace

QString defaultDataRoot()
{
#if defined(Q_OS_WIN)
    const QString roaming = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPDATA"));
    if (!roaming.isEmpty())
        return QDir::cleanPath(QDir(roaming).filePath(QStringLiteral("KomorebiLauncherLite")));
#endif

    return QDir::cleanPath(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                               .filePath(QStringLiteral("KomorebiLauncherLite")));
}

QString dataRootPointer()
{
    return QDir(defaultDataRoot()).filePath(QStringLiteral("data-root.txt"));
}

QString dataRoot()
{
    // An explicit setting wins, then the directory the player picked, then the roaming one on Windows,
    // then Qt's idea of it.
    const QString override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_DATA"));
    if (!override.isEmpty())
        return QDir::cleanPath(override);

    const QString chosen = readDataRootPointer();
    if (!chosen.isEmpty())
        return chosen;

    return defaultDataRoot();
}

bool setDataRoot(const QString& directory, QString* error)
{
    const QString pointer = dataRootPointer();
    if (directory.isEmpty()) {
        // Going back to the default: the pointer file is the whole of the old arrangement.
        if (!QFile::exists(pointer) || QFile::remove(pointer))
            return true;
        if (error)
            *error = QStringLiteral("could not remove %1").arg(QDir::toNativeSeparators(pointer));
        return false;
    }

    const QString target = QDir::cleanPath(QDir(directory).absolutePath());
    if (!QDir().mkpath(target)) {
        if (error)
            *error = QStringLiteral("could not create %1").arg(QDir::toNativeSeparators(target));
        return false;
    }

    // Writing a file there says more than mkpath does: the directory can exist and still be read-only.
    const QString probePath = QDir(target).filePath(QStringLiteral("data-root-probe.tmp"));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly) || probe.write("ok") != 2) {
        if (error)
            *error = QStringLiteral("could not write to %1").arg(QDir::toNativeSeparators(target));
        return false;
    }
    probe.close();
    probe.remove();

    if (!QDir().mkpath(defaultDataRoot())) {
        if (error)
            *error = QStringLiteral("could not create %1").arg(QDir::toNativeSeparators(defaultDataRoot()));
        return false;
    }

    // QSaveFile commits by renaming, so a crash halfway leaves the old pointer rather than nothing.
    QSaveFile file(pointer);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("could not write %1").arg(QDir::toNativeSeparators(pointer));
        return false;
    }
    file.write(QDir::toNativeSeparators(target).toUtf8());
    file.write("\n");
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("could not write %1").arg(QDir::toNativeSeparators(pointer));
        return false;
    }
    return true;
}

bool hasChosenDataRoot()
{
    return !readDataRootPointer().isEmpty();
}

bool dataRootFromEnvironment()
{
    return !QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_DATA")).isEmpty();
}

bool chosenDataRootIsMissing()
{
    // The environment is not a remembered choice: a variable that points nowhere is the caller's own
    // doing, and a test run must not be talked out of the directory it asked for.
    if (dataRootFromEnvironment())
        return false;
    const QString chosen = readDataRootPointer();
    return !chosen.isEmpty() && !QFileInfo(chosen).isDir();
}

bool looksLikeDataRoot(const QString& directory)
{
    const QDir root(directory);
    if (directory.isEmpty() || !root.exists())
        return false;

    static const QStringList markers{ QStringLiteral("settings.json"), QStringLiteral("accounts.json"),
                                      QStringLiteral("instances"),   QStringLiteral("versions"),
                                      QStringLiteral("libraries"),   QStringLiteral("assets"),
                                      QStringLiteral("java") };
    for (const QString& marker : markers) {
        if (QFileInfo(root.filePath(marker)).exists())
            return true;
    }
    return false;
}

QString instancesRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("instances"));
}

QString librariesRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("libraries"));
}

QString assetsRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("assets"));
}

namespace {

/// The form comparison happens in: forward slashes, no trailing separator, no "..".
QString normalised(const QString& path)
{
    QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (clean.endsWith(QLatin1Char('/')) && clean.size() > 1)
        clean.chop(1);
    return clean;
}

}  // namespace

bool isSafeComponent(const QString& value)
{
    if (value.isEmpty() || value == QLatin1String(".") || value == QLatin1String(".."))
        return false;
    if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\')))
        return false;
    if (value.contains(QLatin1Char(':')))
        return false;
    return true;
}

bool staysInside(const QString& root, const QString& target)
{
    const QString base = normalised(root);
    const QString full = normalised(target);
    if (base.isEmpty() || full.isEmpty())
        return false;
    // The separator matters: "C:/libraries2" must not pass as being inside "C:/libraries".
    return full == base || full.startsWith(base + QLatin1Char('/'));
}

QString safeJoin(const QString& root, const QString& relative)
{
    if (relative.isEmpty())
        return {};
    // An absolute path in a relative field is already a lie; refuse before joining.
    if (QDir::isAbsolutePath(relative) || relative.contains(QLatin1Char(':')))
        return {};
    const QString joined = QDir(root).filePath(relative);
    return staysInside(root, joined) ? QDir::cleanPath(joined) : QString();
}

QString versionsRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("versions"));
}

QString versionDirectory(const QString& versionId)
{
    return QDir(versionsRoot()).filePath(versionId);
}
}  // namespace Komorebi