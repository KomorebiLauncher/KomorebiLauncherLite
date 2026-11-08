// SPDX-License-Identifier: GPL-3.0-only
#include "core/util/Scratch.h"

#include <QDir>
#include <QRegularExpression>
#include <QUuid>

#include "core/Paths.h"

namespace Komorebi {

ScratchDirectory::ScratchDirectory(const QString& tag)
{
    QString safe = tag;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
    const QString name = QStringLiteral("%1-%2").arg(safe.isEmpty() ? QStringLiteral("work") : safe,
                                                    QUuid::createUuid().toString(QUuid::Id128));

    // The data directory is the place the launcher knows it can write to. If even that fails -- a
    // roaming profile with a rule against new folders, a data directory left read-only -- the system
    // temp directory is still better than giving up, and the caller is told whether it worked.
    m_path = QDir(dataRoot()).filePath(QStringLiteral("tmp/") + name);
    m_valid = QDir().mkpath(m_path);
    if (!m_valid) {
        m_path = QDir(QDir::tempPath()).filePath(name);
        m_valid = QDir().mkpath(m_path);
    }
}

ScratchDirectory::~ScratchDirectory()
{
    if (m_valid)
        QDir(m_path).removeRecursively();
}

}  // namespace Komorebi
