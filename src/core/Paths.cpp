// SPDX-License-Identifier: GPL-3.0-only
#include "core/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace Komorebi {

QString dataRoot()
{
    // An explicit setting wins, then the roaming directory on Windows, then Qt's idea of it.
    const QString override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_DATA"));
    if (!override.isEmpty())
        return QDir::cleanPath(override);

#if defined(Q_OS_WIN)
    const QString roaming = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPDATA"));
    if (!roaming.isEmpty())
        return QDir::cleanPath(QDir(roaming).filePath(QStringLiteral("KomorebiLauncherLite")));
#endif

    return QDir::cleanPath(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                               .filePath(QStringLiteral("KomorebiLauncherLite")));
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