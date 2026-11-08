// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/LoaderInstaller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/Paths.h"
#include "core/version/VersionJson.h"
#include "core/version/VersionResolver.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kFabricLoader = "https://meta.fabricmc.net/v2/versions/loader";
constexpr auto kQuiltLoader = "https://meta.quiltmc.org/v3/versions/loader";

/// The newest loader version for a game version, or empty when the service knows none.
QString latestLoaderVersion(const QString& base, const QString& minecraftVersion, const QString& kind, QString* error)
{
    const BytesResult bytes = fetchBytes(base);
    if (!bytes.ok) {
        *error = bytes.error;
        return {};
    }

    const auto list = QJsonDocument::fromJson(bytes.data).array();
    for (const auto& entry : list) {
        const auto object = entry.toObject();
        const QString version = object.value(QStringLiteral("version")).toString();
        if (version.isEmpty())
            continue;
        // Both services mark a version as stable or not; take the first stable one we meet.
        if (object.contains(QStringLiteral("stable")) && !object.value(QStringLiteral("stable")).toBool())
            continue;
        Q_UNUSED(kind);
        Q_UNUSED(minecraftVersion);
        return version;
    }

    *error = QStringLiteral("no stable loader version was listed");
    return {};
}

}  // namespace

LoaderResult installLoader(const QString& dataRoot, const QString& minecraftVersion, const LoaderChoice& choice,
                           const std::function<void(const InstallProgress&)>& onProgress)
{
    LoaderResult result;
    if (choice.kind != QLatin1String("fabric") && choice.kind != QLatin1String("quilt")) {
        result.error = QStringLiteral("%1 is not an installed loader").arg(choice.kind);
        return result;
    }

    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    // A loader version that is already on disk needs nothing from the network: a pack that asks for
    // one that is installed must install even when the metadata service is slow or down.
    {
        QString candidate = choice.version;
        if (candidate.isEmpty()) {
            const QDir versions(QDir(dataRoot).filePath(QStringLiteral("versions")));
            const QString prefix = QStringLiteral("%1-loader-").arg(choice.kind);
            for (const auto& entry : versions.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
                if (entry.startsWith(prefix) && entry.endsWith(QLatin1Char('-') + minecraftVersion)) {
                    result.versionId = entry;
                    result.ok = true;
                    return result;
                }
            }
        } else {
            const QString id = QStringLiteral("%1-loader-%2-%3").arg(choice.kind, candidate, minecraftVersion);
            if (QFileInfo::exists(QDir(dataRoot).filePath(QStringLiteral("versions/%1/%1.json").arg(id)))) {
                result.versionId = id;
                result.ok = true;
                return result;
            }
        }
    }

    const QString base = QString::fromLatin1(choice.kind == QLatin1String("fabric") ? kFabricLoader : kQuiltLoader);

    QString loaderVersion = choice.version;
    if (loaderVersion.isEmpty()) {
        report(QStringLiteral("loader"), 0, 0, choice.kind);
        QString error;
        loaderVersion = latestLoaderVersion(base, minecraftVersion, choice.kind, &error);
        if (loaderVersion.isEmpty()) {
            result.error = QStringLiteral("could not ask for a %1 version: %2").arg(choice.kind, error);
            return result;
        }
    }

    // The profile is the whole description of "this loader on this game".
    QString profileUrl;
    if (choice.kind == QLatin1String("fabric")) {
        profileUrl = QStringLiteral("%1/%2/%3/profile/json").arg(base, minecraftVersion, loaderVersion);
    } else {
        profileUrl = QStringLiteral("%1/%2/%3/profile/json").arg(base, loaderVersion, minecraftVersion);
    }

    report(QStringLiteral("loader"), 0, 0, loaderVersion);
    const BytesResult profile = fetchBytes(profileUrl);
    if (!profile.ok) {
        result.error = QStringLiteral("could not fetch the %1 profile: %2").arg(choice.kind, profile.error);
        return result;
    }

    const auto root = QJsonDocument::fromJson(profile.data).object();
    const QString profileId = root.value(QStringLiteral("id")).toString();
    if (profileId.isEmpty() || !isSafeComponent(profileId)) {
        result.error = QStringLiteral("the %1 profile has no usable id").arg(choice.kind);
        return result;
    }

    // Write it where versions live, next to the game it inherits from.
    const QString directory = QDir(dataRoot).filePath(QStringLiteral("versions/%1").arg(profileId));
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(profileId + QStringLiteral(".json"));
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(profile.data) != profile.data.size() || !file.commit()) {
            result.error = QStringLiteral("could not write %1").arg(path);
            return result;
        }
    }

    // The libraries the loader brings have to be on disk before the game starts.
    const ResolvedVersion resolved = resolveVersion(QDir(dataRoot).filePath(QStringLiteral("versions")), profileId);
    if (!resolved.ok()) {
        result.error = resolved.error;
        return result;
    }

    QList<FetchJob> jobs;
    for (const auto& library : resolved.version.libraries) {
        if (!library.artifact.valid())
            continue;
        const QString target = QDir(dataRoot).filePath(QStringLiteral("libraries/") + library.artifact.path);
        if (QFileInfo::exists(target))
            continue;
        jobs.append({ library.artifact.url, target, library.artifact.sha1 });
    }

    const FetchSummary fetched = fetchMany(jobs, 8, [&](int done, int total) {
        report(QStringLiteral("libraries"), done, total, {});
    });
    result.downloaded = fetched.downloaded;
    result.skipped = fetched.skipped;
    result.failed = fetched.failed;
    result.versionId = profileId;
    result.ok = fetched.failed.isEmpty();
    if (!result.ok)
        result.error = QStringLiteral("%1 library file(s) could not be fetched").arg(fetched.failed.size());
    return result;
}

}  // namespace Komorebi
