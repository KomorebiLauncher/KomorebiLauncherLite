// SPDX-License-Identifier: GPL-3.0-only
/*
 *  CurseForge, as far as a launcher needs it: search modpacks, list the files of one, and download a
 *  file. The API wants a key; the launcher ships with the project's own and lets it be replaced.
 */

#pragma once

#include <QList>
#include <QString>

namespace Komorebi {

struct CurseForgeProject {
    int id = 0;
    QString name;
    QString slug;
    QString summary;
    QString logoUrl;
    qint64 downloads = 0;
};

struct CurseForgeFile {
    int id = 0;
    QString remoteId;      ///< the source's own id as text: Modrinth version ids are not numbers
    QString displayName;
    QString fileName;
    QString url;           ///< where the file lives, when the source hands a url out directly
    QString sha1;          ///< lowercase hex of the file, when the source publishes one
    QString version;       ///< the pack's own version string
    QStringList gameVersions;
    QString releaseType;   ///< release, beta, alpha
};

class CurseForge {
   public:
    /// The key in use: the settings value, or the one the project ships with.
    static QString apiKey();

    /// Modpacks matching a search, newest and most downloaded first when `query` is empty.
    static QList<CurseForgeProject> searchModpacks(const QString& query, int pageSize, QString* error);

    /// Files of a project, newest first.
    static QList<CurseForgeFile> files(int projectId, QString* error);

    /// The file itself. `files()` already carries the url for most files; the rest leave it to the
    /// download-url endpoint, and the ones whose author allows no download outside CurseForge at all
    /// are reported as that, not as the 403 they arrive as. `restricted` is set for exactly that
    /// case, so a caller with another source to try knows the retry is worth it.
    static bool downloadFile(int projectId, const CurseForgeFile& file, const QString& target,
                             QString* error, bool* restricted = nullptr);
};

}  // namespace Komorebi
