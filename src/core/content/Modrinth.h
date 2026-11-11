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

    /// The same search, from the `index`th result: what a "load more" under the list asks for.
    static QList<CurseForgeProject> searchModpacks(const QString& query, int pageSize, int index, QString* error);

    /// The versions of a project that fit a game version and a loader, newest first.
    static QList<CurseForgeFile> versions(const QString& projectId, const QString& gameVersion, const QString& loader,
                                          QString* error);

    /// The same list a page at a time, for a picker that asks for more as the player scrolls.
    static QList<CurseForgeFile> versions(const QString& projectId, const QString& gameVersion, const QString& loader,
                                          int offset, int limit, QString* error);

    /// Downloads one version's file. The hash comes from the version record and is checked when it
    /// is not empty, so a truncated or substituted body is refused rather than kept.
    static bool download(const QString& url, const QString& target, const QString& sha1, QString* error);

    /// The same bytes from Modrinth, found by their hash. Authors who allow no download through the
    /// CurseForge API often publish the same file here as well; the hash decides whether it is
    /// really the same file, so a version that carries several files still yields the right one.
    static bool downloadBySha1(const QString& sha1, const QString& target, QString* error);
};

}  // namespace Komorebi
