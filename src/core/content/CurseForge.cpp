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
/// How many files one page of a project's listing holds; the API refuses more than fifty anyway.
constexpr int kFilePage = 50;
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

/// One file record. The file listing and the batch lookup answer with the same shape, so both go
/// through here and a field added once is added everywhere.
CurseForgeFile parseFile(const QJsonObject& item)
{
    CurseForgeFile file;
    file.id = item.value(QStringLiteral("id")).toInt();
    file.displayName = item.value(QStringLiteral("displayName")).toString();
    file.fileName = item.value(QStringLiteral("fileName")).toString();
    file.version = item.value(QStringLiteral("displayName")).toString();
    file.releaseType = QString::number(item.value(QStringLiteral("releaseType")).toInt());
    // Most files carry their own CDN link; the ones that do not get one from the download-url
    // endpoint, or cannot be downloaded at all -- see downloadFile().
    file.url = item.value(QStringLiteral("downloadUrl")).toString();
    // The date is what separates a release from the beta of the next one in a list, and the size is
    // what tells a player whether the file they are picking is the big one.
    file.date = item.value(QStringLiteral("fileDate")).toString();
    file.size = static_cast<qint64>(item.value(QStringLiteral("fileLength")).toDouble());
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
    return file;
}

}  // namespace

QString CurseForge::apiKey()
{
    const QString fromSettings = Settings::load().curseForgeApiKey;
    return fromSettings.isEmpty() ? QString::fromLatin1(kProjectKey) : fromSettings;
}

QList<CurseForgeProject> CurseForge::searchModpacks(const QString& query, int pageSize, QString* error)
{
    return searchModpacks(query, pageSize, 0, error);
}

QList<CurseForgeProject> CurseForge::searchModpacks(const QString& query, int pageSize, int index, QString* error)
{
    QList<CurseForgeProject> projects;

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("gameId"), QString::number(kMinecraft));
    params.addQueryItem(QStringLiteral("classId"), QStringLiteral("4471"));  // modpacks
    params.addQueryItem(QStringLiteral("pageSize"), QString::number(pageSize));
    params.addQueryItem(QStringLiteral("index"), QString::number(qMax(0, index)));
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

CurseForgeProject CurseForge::project(int projectId, QString* error)
{
    CurseForgeProject project;

    const QString url = QStringLiteral("%1/mods/%2").arg(QString::fromLatin1(kBase)).arg(projectId);
    const JsonResult result = getJson(url, headers());
    const QJsonObject object = body(result, error);
    if (!result.ok)
        return project;

    // The file listing names a project by id only. A player told to fetch one file by hand needs the
    // page it is on, and building that page's address needs the slug, which only this endpoint has.
    const auto data = object.value(QStringLiteral("data")).toObject();
    project.id = data.value(QStringLiteral("id")).toInt();
    project.name = data.value(QStringLiteral("name")).toString();
    project.slug = data.value(QStringLiteral("slug")).toString();
    project.summary = data.value(QStringLiteral("summary")).toString();
    return project;
}

QList<CurseForgeFile> CurseForge::files(int projectId, QString* error)
{
    return filesPage(projectId, 0, kFilePage, nullptr, error);
}

QList<CurseForgeFile> CurseForge::filesPage(int projectId, int index, int pageSize, int* totalCount, QString* error)
{
    QList<CurseForgeFile> files;
    if (pageSize <= 0)
        pageSize = kFilePage;

    const QString url = QStringLiteral("%1/mods/%2/files?pageSize=%3&index=%4")
                            .arg(QString::fromLatin1(kBase))
                            .arg(projectId)
                            .arg(pageSize)
                            .arg(qMax(0, index));
    const JsonResult result = getJson(url, headers());
    const QJsonObject object = body(result, error);
    if (!result.ok)
        return files;

    // How many the project has in all, which is the only way the caller knows whether there is a
    // next page: the API answers with fewer files than asked for at the end of a list, not before it.
    if (totalCount != nullptr)
        *totalCount = object.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("totalCount")).toInt();

    for (const auto& entry : object.value(QStringLiteral("data")).toArray()) {
        const CurseForgeFile file = parseFile(entry.toObject());
        if (file.id > 0)
            files.append(file);
    }
    return files;
}

QList<CurseForgeFile> CurseForge::filesByIds(const QList<int>& fileIds, QString* error)
{
    QList<CurseForgeFile> files;
    if (fileIds.isEmpty())
        return files;

    // The endpoint takes a list and answers with one, which is the whole point: a pack of three
    // hundred mods costs one request instead of three hundred. It is kept to batches so a very large
    // pack does not turn into one unreasonable body.
    constexpr int kBatch = 500;
    for (int start = 0; start < fileIds.size(); start += kBatch) {
        QJsonArray ids;
        for (int i = start; i < fileIds.size() && i < start + kBatch; ++i)
            ids.append(fileIds.at(i));
        QJsonObject payload;
        payload.insert(QStringLiteral("fileIds"), ids);

        QList<QPair<QString, QString>> postHeaders = headers();
        postHeaders.append({ QStringLiteral("Content-Type"), QStringLiteral("application/json") });
        const JsonResult result =
            postJson(QStringLiteral("%1/mods/files").arg(QString::fromLatin1(kBase)),
                     QJsonDocument(payload).toJson(QJsonDocument::Compact), postHeaders);
        const QJsonObject object = body(result, error);
        if (!result.ok) {
            // Whatever was already collected is still usable; the caller asks per project for the
            // rest, so one failed batch does not take the whole pack down with it.
            break;
        }
        for (const auto& entry : object.value(QStringLiteral("data")).toArray()) {
            const CurseForgeFile file = parseFile(entry.toObject());
            if (file.id > 0)
                files.append(file);
        }
    }
    return files;
}

bool CurseForge::downloadFile(int projectId, const CurseForgeFile& file, const QString& target, QString* error,
                              bool* restricted)
{
    if (restricted)
        *restricted = false;

    QString downloadUrl = file.url;
    if (downloadUrl.isEmpty()) {
        // Some files come without a link of their own and expect the launcher to ask for one. The
        // ones whose author allows no third-party download answer 403 here with an empty body,
        // which says nothing to a player looking at the reason -- so that case is spelled out. A key
        // that was refused would have failed the listing that produced this file in the first place.
        const QString url = QStringLiteral("%1/mods/%2/files/%3/download-url").arg(QString::fromLatin1(kBase)).arg(projectId).arg(file.id);
        const JsonResult result = getJson(url, headers());
        QString reason;
        const QJsonObject object = body(result, &reason);
        if (!result.ok) {
            if (restricted && result.status == 403)
                *restricted = true;
            *error = result.status == 403
                ? QStringLiteral("the file's author does not allow it to be downloaded outside CurseForge")
                : reason;
            return false;
        }
        downloadUrl = object.value(QStringLiteral("data")).toString();
        if (downloadUrl.isEmpty()) {
            *error = QStringLiteral("CurseForge returned no download url");
            return false;
        }
    }

    const FetchResult fetched = fetchFile(downloadUrl, target, file.sha1);
    if (!fetched.ok) {
        *error = fetched.error;
        return false;
    }
    return true;
}

}  // namespace Komorebi
