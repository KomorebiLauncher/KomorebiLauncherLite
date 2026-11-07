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
        return QDir::cleanPath(QDir(roaming).filePath(QStringLiteral("KomorebiLauncher")));
#endif

    return QDir::cleanPath(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                               .filePath(QStringLiteral("KomorebiLauncher")));
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

}  // namespace Komorebi
