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
    QString date;          ///< published, ISO 8601 as the source writes it; empty when it does not say
    qint64 size = 0;       ///< bytes, 0 when the source does not say
};

class CurseForge {
   public:
    /// The key in use: the settings value, or the one the project ships with.
    static QString apiKey();

    /// Modpacks matching a search, newest and most downloaded first when `query` is empty.
    static QList<CurseForgeProject> searchModpacks(const QString& query, int pageSize, QString* error);

    /// The same search, from the `index`th result: what a "load more" under the list asks for.
    static QList<CurseForgeProject> searchModpacks(const QString& query, int pageSize, int index, QString* error);

    /// Files of a project, newest first.
    static QList<CurseForgeFile> files(int projectId, QString* error);

    /// The same list, one page at a time: `index` files already taken, at most `pageSize` more, and
    /// `totalCount` filled in with how many the project has. A pack with a hundred releases is not
    /// something to show at once, so the caller asks for the rest as the player scrolls.
    static QList<CurseForgeFile> filesPage(int projectId, int index, int pageSize, int* totalCount, QString* error);

    /**
     * Several files by id, in one request.
     *
     * A modpack names its files by id and nothing else, so the launcher has to turn those ids into
     * names and hashes. Listing each project's newest page and searching it is what this replaces:
     * a pack pins the version it was built against, which is regularly older than the newest fifty
     * files of that project, and the ones it misses used to fail the whole install.
     *
     * Files the API no longer knows (deleted by their author) are simply absent from the result.
     */
    static QList<CurseForgeFile> filesByIds(const QList<int>& fileIds, QString* error);

    /// One project by id: its name and its slug, which is what a link to a file's page is made of.
    static CurseForgeProject project(int projectId, QString* error);

    /// The file itself. `files()` already carries the url for most files; the rest leave it to the
    /// download-url endpoint, and the ones whose author allows no download outside CurseForge at all
    /// are reported as that, not as the 403 they arrive as. `restricted` is set for exactly that
    /// case, so a caller with another source to try knows the retry is worth it.
    static bool downloadFile(int projectId, const CurseForgeFile& file, const QString& target,
                             QString* error, bool* restricted = nullptr);
};

}  // namespace Komorebi
