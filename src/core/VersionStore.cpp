// SPDX-License-Identifier: GPL-3.0-only
#include "core/VersionStore.h"

#include <QDir>
#include <QFileInfo>

#include "core/Paths.h"
#include "core/version/VersionJson.h"

namespace Komorebi {

QString InstalledVersion::summary() const
{
    QStringList parts;
    if (javaMajor > 0)
        parts << QStringLiteral("Java %1").arg(javaMajor);
    if (!mainClass.isEmpty())
        parts << mainClass.section(QLatin1Char('.'), -1);
    if (size > 0)
        parts << QStringLiteral("%1 MB").arg(QString::number(size / (1024.0 * 1024.0), 'f', 0));
    return parts.join(QStringLiteral(" · "));
}

VersionStore::VersionStore(QString versionsRoot)
    : m_versionsRoot(std::move(versionsRoot))
{
}

QList<InstalledVersion> VersionStore::scan() const
{
    QList<InstalledVersion> versions;

    const QDir root(m_versionsRoot);
    if (!root.exists())
        return versions;

    const auto entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& entry : entries) {
        if (!isSafeComponent(entry))
            continue;
        const auto version = find(entry);
        if (!version.id.isEmpty())
            versions.append(version);
    }
    return versions;
}

InstalledVersion VersionStore::find(const QString& id) const
{
    InstalledVersion version;
    if (!isSafeComponent(id))
        return version;

    const QString directory = QDir(m_versionsRoot).filePath(id);
    const QString description = QDir(directory).filePath(id + QStringLiteral(".json"));
    if (!QFileInfo::exists(description))
        return version;

    const VersionJson json = VersionJson::fromFile(description);
    if (json.id.isEmpty())
        return version;

    version.id = id;
    version.path = directory;
    version.mainClass = json.mainClass;
    version.javaMajor = json.javaMajor;
    version.hasGameJar = QFileInfo::exists(QDir(directory).filePath(id + QStringLiteral(".jar")));

    // The size of the version's own directory, which is what a player thinks of as "this version".
    const auto files = QDir(directory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto& file : files)
        version.size += file.size();

    return version;
}

}  // namespace Komorebi
