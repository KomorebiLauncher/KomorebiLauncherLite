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
    m_path = QDir(dataRoot())
                 .filePath(QStringLiteral("tmp/%1-%2").arg(safe.isEmpty() ? QStringLiteral("work") : safe,
                                                           QUuid::createUuid().toString(QUuid::Id128)));
    m_valid = QDir().mkpath(m_path);
}

ScratchDirectory::~ScratchDirectory()
{
    if (m_valid)
        QDir(m_path).removeRecursively();
}

}  // namespace Komorebi
