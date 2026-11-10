// SPDX-License-Identifier: GPL-3.0-only
#include "core/content/CurseForge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include "core/Settings.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kBase = "https://api.curseforge.com/v1";
constexpr int kMinecraft = 432;
/// The hash algorithm numbers of the file API: 1 is sha1, 2 is md5. The wire format is a number.
constexpr int kSha1 = 1;

/// The key the project registered; a launcher cannot ask people for one, so it ships with it.
constexpr auto kProjectKey = "$2a$10$hv6wkwyWxo2qzitWqO7NQ.eMQ8aCiMVSM3l53g90MgTOtZL/7t/mK";

QList<QPair<QString, QString>> headers()
{
    return {
        { QStringLiteral("x-api-key"), CurseForge::apiKey() },
        { QStringLiteral("Accept"), QStringLiteral("application/json") },
    };
}

QJsonObject body(const JsonResult& result, QString* error)
{
    const QJsonObject object = QJsonDocument::fromJson(result.body).object();
    if (!result.ok) {
        *error = object.value(QStringLiteral("error")).toString(result.error);
        if (error->isEmpty())
            *error = QStringLiteral("CurseForge answered %1").arg(result.status);
    }
    return object;
}

}  // namespace

QString CurseForge::apiKey()
{
    const QString fromSettings = Settings::load().curseForgeApiKey;
    return fromSettings.isEmpty() ? QString::fromLatin1(kProjectKey) : fromSettings;
}

QList<CurseForgeProject> CurseForge::searchModpacks(const QString& query, int pageSize, QString* error)
{
    QList<CurseForgeProject> projects;

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("gameId"), QString::number(kMinecraft));
    params.addQueryItem(QStringLiteral("classId"), QStringLiteral("4471"));  // modpacks
    params.addQueryItem(QStringLiteral("pageSize"), QString::number(pageSize));
    params.addQueryItem(QStringLiteral("sortField"), QStringLiteral("2"));  // popularity
    params.addQueryItem(QStringLiteral("sortOrder"), QStringLiteral("desc"));
    if (!query.isEmpty())
        params.addQueryItem(QStringLiteral("searchFilter"), query);

    const QString url = QStringLiteral("%1/mods/search?%2").arg(QString::fromLatin1(kBase), params.toString(QUrl::FullyEncoded));
    const JsonResult result = getJson(url, headers());
    const QJsonObject object = body(result, error);
    if (!result.ok)
        return projects;

    for (const auto& entry : object.value(QStringLiteral("data")).toArray()) {
        const auto item = entry.toObject();
        CurseForgeProject project;
        project.id = item.value(QStringLiteral("id")).toInt();
        project.name = item.value(QStringLiteral("name")).toString();
        project.slug = item.value(QStringLiteral("slug")).toString();
        project.summary = item.value(QStringLiteral("summary")).toString();
        project.logoUrl = item.value(QStringLiteral("logo")).toObject().value(QStringLiteral("thumbnailUrl")).toString();
        project.downloads = static_cast<qint64>(item.value(QStringLiteral("downloadCount")).toDouble());
        if (project.id > 0)
            projects.append(project);
    }
    return projects;
}

QList<CurseForgeFile> CurseForge::files(int projectId, QString* error)
{
    QList<CurseForgeFile> files;

    const QString url = QStringLiteral("%1/mods/%2/files?pageSize=50").arg(QString::fromLatin1(kBase)).arg(projectId);
    const JsonResult result = getJson(url, headers());
    const QJsonObject object = body(result, error);
    if (!result.ok)
        return files;

    for (const auto& entry : object.value(QStringLiteral("data")).toArray()) {
        const auto item = entry.toObject();
        CurseForgeFile file;
        file.id = item.value(QStringLiteral("id")).toInt();
        file.displayName = item.value(QStringLiteral("displayName")).toString();
        file.fileName = item.value(QStringLiteral("fileName")).toString();
        file.version = item.value(QStringLiteral("displayName")).toString();
        file.releaseType = QString::number(item.value(QStringLiteral("releaseType")).toInt());
        for (const auto& version : item.value(QStringLiteral("gameVersions")).toArray())
            file.gameVersions << version.toString();
        // The API lists the hashes as (value, algo) pairs, and algo is a number: 1 is sha1, 2 is md5.
        // The sha1 is passed on so a download can be checked against the file the API described, not
        // just its size.
        for (const auto& hash : item.value(QStringLiteral("hashes")).toArray()) {
            const auto record = hash.toObject();
            if (record.value(QStringLiteral("algo")).toInt() == kSha1) {
                file.sha1 = record.value(QStringLiteral("value")).toString();
                break;
            }
        }
        if (file.id > 0)
            files.append(file);
    }
    return files;
}

bool CurseForge::downloadFile(int projectId, int fileId, const QString& target, const QString& sha1, QString* error)
{
    // The API hands out a signed url for the file, which is what keeps the CDN link stable.
    const QString url = QStringLiteral("%1/mods/%2/files/%3/download-url").arg(QString::fromLatin1(kBase)).arg(projectId).arg(fileId);
    const JsonResult result = getJson(url, headers());
    const QJsonObject object = body(result, error);
    if (!result.ok)
        return false;

    const QString downloadUrl = object.value(QStringLiteral("data")).toString();
    if (downloadUrl.isEmpty()) {
        *error = QStringLiteral("CurseForge returned no download url");
        return false;
    }

    const FetchResult fetched = fetchFile(downloadUrl, target, sha1);
    if (!fetched.ok) {
        *error = fetched.error;
        return false;
    }
    return true;
}

}  // namespace Komorebi
