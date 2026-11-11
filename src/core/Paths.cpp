// SPDX-License-Identifier: GPL-3.0-only
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace Komorebi {

namespace {

/// Whether `directory` takes a file: it can exist and still be read-only, so this writes one.
bool directoryIsWritable(const QString& directory, QString* error)
{
    const QString probePath = QDir(directory).filePath(QStringLiteral("komorebi-write-probe.tmp"));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QCoreApplication::translate("Paths", "a file could not be written into it");
        return false;
    }
    const bool wrote = probe.write("ok") == 2;
    probe.close();
    probe.remove();
    if (!wrote && error)
        *error = QCoreApplication::translate("Paths", "a file could not be written into it");
    return wrote;
}

}  // namespace

QString defaultDataRoot()
{
    // The launcher is portable and has exactly one data directory: the folder it was started from.
    // Nothing records another one, so copying the folder carries the instances, the accounts and the
    // saves along with it, and there is no file whose loss could make the launcher forget where its
    // data is.
    return QDir::cleanPath(QCoreApplication::applicationDirPath());
}

QString dataRoot()
{
    // The environment variable is for the test harness and the package scripts, which run several
    // copies against throwaway directories: without it they would all want the same folder, and
    // without an override a check would write into a real installation.
    const QString override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_DATA"));
    if (!override.isEmpty())
        return QDir::cleanPath(override);

    return defaultDataRoot();
}

bool dataRootIsWritable(QString* error)
{
    const QString root = dataRoot();
    if (!QDir().mkpath(root)) {
        if (error)
            *error = QCoreApplication::translate("Paths", "the folder could not be created");
        return false;
    }
    return directoryIsWritable(root, error);
}

bool dataRootFromEnvironment()
{
    return !QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_DATA")).isEmpty();
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
