// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/VersionInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include "core/Paths.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

QString readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool writeAll(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(data) != data.size())
        return false;
    return file.commit();
}

}  // namespace

QString InstallResult::summary() const
{
    if (!ok)
        return QStringLiteral("failed: %1 (%2 file(s) could not be fetched)").arg(error).arg(failed.size());
    return QStringLiteral("ok: %1 downloaded, %2 already present").arg(downloaded).arg(skipped);
}

VersionManifest VersionManifest::fetch(QString* error)
{
    VersionManifest manifest;

    const BytesResult bytes = fetchBytes(QString::fromLatin1(kManifestUrl));
    if (!bytes.ok) {
        if (error)
            *error = bytes.error;
        return manifest;
    }

    const auto root = QJsonDocument::fromJson(bytes.data).object();
    const auto latest = root.value(QStringLiteral("latest")).toObject();
    manifest.latestRelease = latest.value(QStringLiteral("release")).toString();
    manifest.latestSnapshot = latest.value(QStringLiteral("snapshot")).toString();

    for (const auto& entry : root.value(QStringLiteral("versions")).toArray()) {
        const auto object = entry.toObject();
        RemoteVersion version;
        version.id = object.value(QStringLiteral("id")).toString();
        version.type = object.value(QStringLiteral("type")).toString();
        version.url = object.value(QStringLiteral("url")).toString();
        version.releaseTime = object.value(QStringLiteral("releaseTime")).toString();
        if (!version.id.isEmpty() && !version.url.isEmpty())
            manifest.versions.append(version);
    }

    if (manifest.versions.isEmpty() && error)
        *error = QStringLiteral("the version list came back empty");
    return manifest;
}

RemoteVersion VersionManifest::find(const QString& id) const
{
    for (const auto& version : versions) {
        if (version.id == id)
            return version;
    }
    return {};
}

VersionInstaller::VersionInstaller(QString dataRoot)
    : m_dataRoot(std::move(dataRoot))
{
}

QString VersionInstaller::versionDirectory(const QString& versionId) const
{
    return QDir(m_dataRoot).filePath(QStringLiteral("versions/%1").arg(versionId));
}

InstallResult VersionInstaller::install(const QString& versionId, bool withAssets)
{
    InstallResult result;
    if (!isSafeComponent(versionId)) {
        result.error = QStringLiteral("%1 is not a version id").arg(versionId);
        return result;
    }

    const auto report = [this](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    report(QStringLiteral("manifest"), 0, 0, {});
    QString error;
    const VersionManifest manifest = VersionManifest::fetch(&error);
    if (manifest.versions.isEmpty()) {
        result.error = QStringLiteral("version list: %1").arg(error);
        return result;
    }

    const RemoteVersion version = manifest.find(versionId);
    if (version.id.isEmpty()) {
        result.error = QStringLiteral("version %1 is not in the list").arg(versionId);
        return result;
    }

    return installFrom(version, withAssets);
}

InstallResult VersionInstaller::installFrom(const RemoteVersion& version, bool withAssets)
{
    InstallResult result;
    const auto report = [this](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };
    const auto directory = versionDirectory(version.id);

    // 1. the description
    report(QStringLiteral("version"), 0, 0, version.id);
    const BytesResult description = fetchBytes(version.url);
    if (!description.ok) {
        result.error = QStringLiteral("version %1: %2").arg(version.id, description.error);
        return result;
    }
    const QString versionJson = QDir(directory).filePath(version.id + QStringLiteral(".json"));
    if (!writeAll(versionJson, description.data)) {
        result.error = QStringLiteral("cannot write %1").arg(versionJson);
        return result;
    }

    const VersionJson parsed = VersionJson::fromJson(description.data);
    if (parsed.id.isEmpty()) {
        result.error = QStringLiteral("version %1 has a description that cannot be read").arg(version.id);
        return result;
    }

    // 2. the game itself
    const QString jar = QDir(directory).filePath(version.id + QStringLiteral(".jar"));
    if (parsed.clientJar.valid()) {
        const FetchResult fetched = fetchFile(parsed.clientJar.url, jar, parsed.clientJar.sha1);
        if (!fetched.ok) {
            result.failed << QStringLiteral("%1 (%2)").arg(jar, fetched.error);
            result.error = QStringLiteral("the game could not be downloaded");
            return result;
        }
        fetched.alreadyPresent ? ++result.skipped : ++result.downloaded;
    }

    // 3. the libraries: the parser already dropped the ones this platform does not need
    const QList<Library>& wanted = parsed.libraries;
    int done = 0;
    for (const auto& library : wanted) {
        const Download& download = library.isNative() ? library.natives : library.artifact;
        if (!download.valid())
            continue;
        const QString target = QDir(m_dataRoot).filePath(QStringLiteral("libraries/") + download.path);
        report(QStringLiteral("libraries"), done++, wanted.size(), library.name);
        const FetchResult fetched = fetchFile(download.url, target, download.sha1);
        if (!fetched.ok) {
            result.failed << QStringLiteral("%1 (%2)").arg(target, fetched.error);
            continue;
        }
        fetched.alreadyPresent ? ++result.skipped : ++result.downloaded;
    }

    // 4. the asset index, and then the assets it lists
    if (parsed.assetIndex.valid()) {
        const QString indexPath = QDir(m_dataRoot).filePath(QStringLiteral("assets/") + parsed.assetIndex.path);
        report(QStringLiteral("assets index"), 0, 0, parsed.assetIndex.path);
        const FetchResult fetched = fetchFile(parsed.assetIndex.url, indexPath, parsed.assetIndex.sha1);
        if (!fetched.ok) {
            result.failed << QStringLiteral("%1 (%2)").arg(indexPath, fetched.error);
        } else {
            fetched.alreadyPresent ? ++result.skipped : ++result.downloaded;
        }

        if (withAssets && fetched.ok) {
            const auto index = QJsonDocument::fromJson(readAll(indexPath).toUtf8()).object();
            const auto objects = index.value(QStringLiteral("objects")).toObject();
            int assetDone = 0;
            const int assetTotal = objects.size();
            for (auto it = objects.begin(); it != objects.end(); ++it) {
                const auto object = it.value().toObject();
                const QString hash = object.value(QStringLiteral("hash")).toString();
                if (hash.size() < 2)
                    continue;
                const QString target = QDir(m_dataRoot).filePath(
                    QStringLiteral("assets/objects/%1/%2").arg(hash.left(2), hash));
                report(QStringLiteral("assets"), assetDone++, assetTotal, it.key());
                if (QFileInfo::exists(target)) {
                    ++result.skipped;
                    continue;
                }
                const QString url = QStringLiteral("https://resources.download.minecraft.net/%1/%2").arg(hash.left(2), hash);
                const FetchResult asset = fetchFile(url, target, hash);
                if (!asset.ok) {
                    result.failed << QStringLiteral("%1 (%2)").arg(target, asset.error);
                    continue;
                }
                asset.alreadyPresent ? ++result.skipped : ++result.downloaded;
            }
        }
    }

    result.ok = result.failed.isEmpty();
    if (!result.ok && result.error.isEmpty())
        result.error = QStringLiteral("%1 file(s) could not be fetched").arg(result.failed.size());
    return result;
}

}  // namespace Komorebi
