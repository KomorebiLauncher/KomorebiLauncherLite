// SPDX-License-Identifier: GPL-3.0-only
#include "core/content/Modrinth.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kBase = "https://api.modrinth.com/v2";

/// Modrinth asks for a user agent that says who is calling; requests without one are refused.
QList<QPair<QString, QString>> headers()
{
    return {
        { QStringLiteral("Accept"), QStringLiteral("application/json") },
        { QStringLiteral("User-Agent"), QStringLiteral("KomorebiLite/0.1 (github.com/KomorebiLauncher/KomorebiLauncherLite)") },
    };
}

}  // namespace

QList<CurseForgeProject> Modrinth::searchModpacks(const QString& query, int pageSize, QString* error)
{
    QList<CurseForgeProject> projects;

    QUrlQuery params;
    if (!query.isEmpty())
        params.addQueryItem(QStringLiteral("query"), query);
    params.addQueryItem(QStringLiteral("limit"), QString::number(pageSize));
    params.addQueryItem(QStringLiteral("facets"), QStringLiteral("[[\"project_type:modpack\"]]"));

    const QString url = QStringLiteral("%1/search?%2").arg(QString::fromLatin1(kBase), params.toString(QUrl::FullyEncoded));
    const JsonResult result = getJson(url, headers());
    if (!result.ok) {
        *error = QStringLiteral("Modrinth answered %1").arg(result.status);
        return projects;
    }

    const QJsonObject object = QJsonDocument::fromJson(result.body).object();
    for (const auto& entry : object.value(QStringLiteral("hits")).toArray()) {
        const auto item = entry.toObject();
        CurseForgeProject project;
        project.slug = item.value(QStringLiteral("project_id")).toString();
        project.name = item.value(QStringLiteral("title")).toString();
        project.summary = item.value(QStringLiteral("description")).toString();
        project.logoUrl = item.value(QStringLiteral("icon_url")).toString();
        project.downloads = static_cast<qint64>(item.value(QStringLiteral("downloads")).toDouble());
        if (!project.slug.isEmpty())
            projects.append(project);
    }
    return projects;
}

QList<CurseForgeFile> Modrinth::versions(const QString& projectId, const QString& gameVersion, const QString& loader,
                                         QString* error)
{
    QList<CurseForgeFile> files;

    QUrlQuery params;
    if (!gameVersion.isEmpty())
        params.addQueryItem(QStringLiteral("game_versions"), QStringLiteral("[\"%1\"]").arg(gameVersion));
    if (!loader.isEmpty())
        params.addQueryItem(QStringLiteral("loaders"), QStringLiteral("[\"%1\"]").arg(loader));

    const QString url = QStringLiteral("%1/project/%2/version?%3")
                            .arg(QString::fromLatin1(kBase), projectId, params.toString(QUrl::FullyEncoded));
    const JsonResult result = getJson(url, headers());
    if (!result.ok) {
        *error = QStringLiteral("Modrinth answered %1").arg(result.status);
        return files;
    }

    for (const auto& entry : QJsonDocument::fromJson(result.body).array()) {
        const auto item = entry.toObject();
        CurseForgeFile file;
        file.remoteId = item.value(QStringLiteral("id")).toString();
        file.displayName = item.value(QStringLiteral("name")).toString();
        file.version = item.value(QStringLiteral("version_number")).toString();
        file.releaseType = item.value(QStringLiteral("version_type")).toString();
        for (const auto& game : item.value(QStringLiteral("game_versions")).toArray())
            file.gameVersions << game.toString();

        // The file itself is the first entry of `files`, which carries the url and the hashes.
        const auto list = item.value(QStringLiteral("files")).toArray();
        if (!list.isEmpty()) {
            const auto entryFile = list.first().toObject();
            file.url = entryFile.value(QStringLiteral("url")).toString();
            file.fileName = QFileInfo(entryFile.value(QStringLiteral("filename")).toString()).fileName();
            file.sha1 = entryFile.value(QStringLiteral("hashes")).toObject().value(QStringLiteral("sha1")).toString();
        }
        if (!file.url.isEmpty())
            files.append(file);
    }
    return files;
}

bool Modrinth::download(const QString& url, const QString& target, const QString& sha1, QString* error)
{
    const FetchResult fetched = fetchFile(url, target, sha1);
    if (!fetched.ok) {
        *error = fetched.error;
        return false;
    }
    return true;
}

}  // namespace Komorebi
