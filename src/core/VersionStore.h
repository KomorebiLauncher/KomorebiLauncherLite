// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The versions this launcher has installed. One directory each, the description inside it -- the
 *  same shape the game uses, so a folder written here can be opened by any launcher.
 */

#pragma once

#include <QList>
#include <QString>

namespace Komorebi {

struct InstalledVersion {
    QString id;
    QString path;         ///< versions/<id>
    QString mainClass;
    int javaMajor = 0;
    qint64 size = 0;      ///< the version's own files, in bytes
    bool hasGameJar = false;

    QString summary() const;
};

class VersionStore {
   public:
    explicit VersionStore(QString versionsRoot);

    /// Every version directory that has a readable description, newest name first.
    QList<InstalledVersion> scan() const;

    /// One version, or an empty id when it is not installed.
    InstalledVersion find(const QString& id) const;

   private:
    QString m_versionsRoot;
};

}  // namespace Komorebi
