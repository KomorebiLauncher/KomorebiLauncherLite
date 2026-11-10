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

/// The path a pointer file names, or an empty string when it says nothing this launcher can use.
QString readPointer(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString line = QString::fromUtf8(file.readAll()).trimmed();
    // A relative path in here would depend on the working directory the launcher was started from,
    // which is not something a player can be expected to control.
    if (line.isEmpty() || !QDir::isAbsolutePath(line))
        return {};
    return QDir::cleanPath(line);
}

/// Writes a pointer file, replacing whatever was there. False when it could not be written.
bool writePointer(const QString& path, const QString& directory)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    // QSaveFile commits by renaming, so a crash halfway leaves the old pointer rather than nothing.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(QDir::toNativeSeparators(directory).toUtf8());
    file.write("\n");
    return file.commit();
}

/// A pointer file that is not there is already forgotten.
bool removePointer(const QString& path)
{
    return !QFile::exists(path) || QFile::remove(path);
}

/**
 * The second copy of the pointer, deliberately outside the folder the first one lives in.
 *
 * The first copy has to sit in `defaultDataRoot()`, which is also the folder a player has just
 * emptied by copying its contents somewhere else — and deleting it therefore silently forgot which
 * directory the launcher had been moved to. This one survives that.
 */
QString shadowDataRootPointer()
{
#if defined(Q_OS_WIN)
    const QString local = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (!local.isEmpty())
        return QDir::cleanPath(QDir(local).filePath(QStringLiteral("KomorebiLauncherLite/data-root.txt")));
#endif

    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
        .filePath(QStringLiteral("KomorebiLauncherLite/data-root.txt"));
}

/// The remembered data directory, or an empty string when there is none worth using.
QString readDataRootPointer()
{
    return readPointer(dataRootPointer());
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
        // Going back to the default: the pointer files are the whole of the old arrangement, and both
        // copies have to go or the second one would keep offering the arrangement back.
        if (removePointer(pointer) && removePointer(shadowDataRootPointer()))
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
    if (!writePointer(pointer, target)) {
        if (error)
            *error = QStringLiteral("could not write %1").arg(QDir::toNativeSeparators(pointer));
        return false;
    }

    // Best effort on purpose: the second copy only makes a question askable, it never decides where
    // the launcher looks, so a read-only local profile is not a reason to refuse the switch.
    writePointer(shadowDataRootPointer(), target);
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

QString forgottenDataRoot()
{
    // While the real pointer is there it is the whole answer, whatever the second copy says.
    if (!readDataRootPointer().isEmpty())
        return {};

    const QString remembered = readPointer(shadowDataRootPointer());
    if (remembered.isEmpty() || remembered == QDir::cleanPath(defaultDataRoot()))
        return {};
    return remembered;
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