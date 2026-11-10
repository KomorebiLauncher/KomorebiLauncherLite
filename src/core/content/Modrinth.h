// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Modrinth, which needs no key: search, the versions of a project, and the file itself.
 *
 *  A Modrinth modpack is a .mrpack zip with `modrinth.index.json` in it -- the game version, the
 *  loader, and a list of files with their own download urls and hashes.
 */

#pragma once

#include <QList>
#include <QString>

#include "core/content/CurseForge.h"

namespace Komorebi {

class Modrinth {
   public:
    /// Modpacks matching a query.
    static QList<CurseForgeProject> searchModpacks(const QString& query, int pageSize, QString* error);

    /// The versions of a project that fit a game version and a loader, newest first.
    static QList<CurseForgeFile> versions(const QString& projectId, const QString& gameVersion, const QString& loader,
                                          QString* error);

    /// Downloads one version's file. The hash comes from the version record and is checked when it
    /// is not empty, so a truncated or substituted body is refused rather than kept.
    static bool download(const QString& url, const QString& target, const QString& sha1, QString* error);
};

}  // namespace Komorebi
