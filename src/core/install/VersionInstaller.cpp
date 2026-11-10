// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/VersionInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include "core/JavaScan.h"
#include "core/Paths.h"
#include "core/install/JavaRuntime.h"
#include "core/version/VersionResolver.h"
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
    const QString line = QStringLiteral("ok: %1 downloaded, %2 already present").arg(downloaded).arg(skipped);
    return warning.isEmpty() ? line : line + QStringLiteral("\nwarning: ") + warning;
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
    // `install()` checks the id the player asked for; this one uses the id the remote description
    // carries, which is the other way a "version" name reaches versions/<id>.
    if (!isSafeComponent(version.id)) {
        result.error = QStringLiteral("%1 is not a version id").arg(version.id);
        return result;
    }
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
    QList<FetchJob> libraryJobs;
    const QString librariesDirectory = QDir(m_dataRoot).filePath(QStringLiteral("libraries"));
    for (const auto& library : parsed.libraries) {
        const Download& download = library.isNative() ? library.natives : library.artifact;
        if (!download.valid())
            continue;
        // The path is written by a server, so it decides nothing by itself: it has to stay inside
        // libraries/. safeJoin refuses "..", an absolute path and a drive letter.
        const QString target = safeJoin(librariesDirectory, download.path);
        if (target.isEmpty()) {
            result.failed << QStringLiteral("%1 (not a path inside the libraries directory)").arg(download.path);
            continue;
        }
        libraryJobs.append({ download.url, target, download.sha1 });
    }
    const FetchSummary libraryResult = fetchMany(libraryJobs, 8, [&](int done, int total) {
        report(QStringLiteral("libraries"), done, total, {});
    });
    result.downloaded += libraryResult.downloaded;
    result.skipped += libraryResult.skipped;
    result.failed += libraryResult.failed;
    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        return result;
    }

    // 4. the runtime the version was built against, when this machine has no matching one. Without it
    // the game cannot start at all, and downloading it here is cheaper than failing at launch. A
    // system Java of the same major counts: there is no reason to fetch a second copy of what is
    // already installed.
    const auto hasMatchingJava = [this](int major) {
        if (hasJavaRuntime(m_dataRoot, major))
            return true;
        for (const auto& runtime : JavaScan::find()) {
            if (runtime.major == major)
                return true;
        }
        return false;
    };
    if (!parsed.javaComponent.isEmpty() && !hasMatchingJava(parsed.javaMajor)) {
        report(QStringLiteral("java"), 0, 0, parsed.javaComponent);
        const JavaRuntimeResult runtime = installJavaRuntime(m_dataRoot, parsed.javaComponent, parsed.javaMajor,
                                                             [&](const InstallProgress& progress) {
                                                                 report(progress.stage, progress.done, progress.total, progress.current);
                                                             });
        result.downloaded += runtime.downloaded;
        result.skipped += runtime.skipped;
        if (!runtime.ok) {
            // The game is installed; a runtime that could not be fetched is reported rather than fatal:
            // the player may have their own Java, and the launch path picks whichever fits.
            result.warning = QStringLiteral("Java %1 could not be downloaded: %2").arg(parsed.javaMajor).arg(runtime.error);
        }
    }
    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        return result;
    }

    // 5. the asset index, and then the assets it lists
    const QString assetsDirectory = QDir(m_dataRoot).filePath(QStringLiteral("assets"));
    if (parsed.assetIndex.path.isEmpty()) {
        // A description that names an index but whose id could not be used as a file name reaches
        // here: saying nothing would report a successful install with no assets in it.
        if (!parsed.assetIndex.id.isEmpty() || !parsed.assetIndex.url.isEmpty())
            result.failed << QStringLiteral("%1 (an asset index id must be a file name)").arg(parsed.assetIndex.id);
    } else {
        const QString indexPath = safeJoin(assetsDirectory, parsed.assetIndex.path);
        if (indexPath.isEmpty()) {
            // The index name is not usable as one: refuse it instead of reading or writing wherever
            // it points. This leaves the install reported as failed, which is the honest answer.
            result.failed << QStringLiteral("%1 (not a path inside the assets directory)").arg(parsed.assetIndex.path);
        } else {
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
                QList<FetchJob> assetJobs;
                assetJobs.reserve(objects.size());
                // An object is named by its sha1 and stored under its first two characters. Anything
                // that is not forty hex digits is not a hash, so it is refused rather than used as a
                // path -- a hash is the one field of an asset index that becomes a file name.
                static const QRegularExpression hashPattern(QStringLiteral("\\A[0-9a-f]{40}\\z"));
                const QString objectsDirectory = QDir(assetsDirectory).filePath(QStringLiteral("objects"));
                for (auto it = objects.begin(); it != objects.end(); ++it) {
                    const QString hash = it.value().toObject().value(QStringLiteral("hash")).toString();
                    if (!hashPattern.match(hash).hasMatch()) {
                        result.failed << QStringLiteral("%1 (not an asset hash)").arg(it.key());
                        continue;
                    }
                    const QString target = safeJoin(objectsDirectory, QStringLiteral("%1/%2").arg(hash.left(2), hash));
                    if (target.isEmpty()) {
                        result.failed << QStringLiteral("%1 (not a path inside the assets directory)").arg(hash);
                        continue;
                    }
                    assetJobs.append({ QStringLiteral("https://resources.download.minecraft.net/%1/%2").arg(hash.left(2), hash), target, hash });
                }
                const FetchSummary assetResult = fetchMany(assetJobs, 12, [&](int done, int total) {
                    report(QStringLiteral("assets"), done, total, {});
                });
                result.downloaded += assetResult.downloaded;
                result.skipped += assetResult.skipped;
                result.failed += assetResult.failed;
            }
        }
    }

    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        return result;
    }
    result.ok = result.failed.isEmpty();
    if (!result.ok && result.error.isEmpty())
        result.error = QStringLiteral("%1 file(s) could not be fetched").arg(result.failed.size());
    return result;
}

InstallResult VersionInstaller::repair(const QString& versionId)
{
    InstallResult result;
    const auto report = [this](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    const QString versionsRoot = QDir(m_dataRoot).filePath(QStringLiteral("versions"));
    report(QStringLiteral("checking"), 0, 0, versionId);
    const ResolvedVersion resolved = resolveVersion(versionsRoot, versionId);
    if (!resolved.ok()) {
        result.error = resolved.error;
        return result;
    }
    // The chain merged into one description: the jar and the asset index come from the vanilla
    // version at its bottom, the libraries from every step.
    const VersionJson& version = resolved.version;

    QList<FetchJob> jobs;

    // 1. the game itself, which lives beside the version that owns it, not beside the loader
    const QString jarPath = QDir(versionDirectory(resolved.baseId)).filePath(resolved.baseId + QStringLiteral(".jar"));
    if (version.clientJar.valid() && !QFileInfo::exists(jarPath))
        jobs.append({ version.clientJar.url, jarPath, version.clientJar.sha1 });

    // 2. libraries, including the ones a loader brought with it. The path comes from the merged
    // description, so it is checked against the libraries directory before anything is fetched: a
    // repair reads the same metadata an install does, and a tampered one must not decide the target.
    const QString librariesDirectory = QDir(m_dataRoot).filePath(QStringLiteral("libraries"));
    const auto addFile = [&](const Download& download) {
        if (!download.valid())
            return;
        const QString target = safeJoin(librariesDirectory, download.path);
        if (target.isEmpty()) {
            result.failed << QStringLiteral("%1 (not a path inside the libraries directory)").arg(download.path);
            return;
        }
        if (QFileInfo::exists(target))
            return;
        jobs.append({ download.url, target, download.sha1 });
    };
    for (const auto& library : version.libraries)
        addFile(library.isNative() ? library.natives : library.artifact);
    for (const auto& library : version.mavenFiles)
        addFile(library.isNative() ? library.natives : library.artifact);

    // 3. the asset index, then the objects it points at
    const QString assetsDirectory = QDir(m_dataRoot).filePath(QStringLiteral("assets"));
    if (version.assetIndex.path.isEmpty()) {
        if (!version.assetIndex.id.isEmpty() || !version.assetIndex.url.isEmpty())
            result.failed << QStringLiteral("%1 (an asset index id must be a file name)").arg(version.assetIndex.id);
    } else {
        const QString indexPath = safeJoin(assetsDirectory, version.assetIndex.path);
        if (indexPath.isEmpty()) {
            result.failed << QStringLiteral("%1 (not a path inside the assets directory)").arg(version.assetIndex.path);
        } else {
            if (!QFileInfo::exists(indexPath)) {
                QDir().mkpath(QFileInfo(indexPath).absolutePath());
                const FetchResult fetched = fetchFile(version.assetIndex.url, indexPath, version.assetIndex.sha1);
                if (!fetched.ok) {
                    result.failed << QStringLiteral("%1 (%2)").arg(indexPath, fetched.error);
                } else {
                    fetched.alreadyPresent ? ++result.skipped : ++result.downloaded;
                }
            }
            const auto index = QJsonDocument::fromJson(readAll(indexPath).toUtf8()).object();
            const auto objects = index.value(QStringLiteral("objects")).toObject();
            static const QRegularExpression hashPattern(QStringLiteral("\\A[0-9a-f]{40}\\z"));
            const QString objectsDirectory = QDir(assetsDirectory).filePath(QStringLiteral("objects"));
            for (auto it = objects.begin(); it != objects.end(); ++it) {
                const QString hash = it.value().toObject().value(QStringLiteral("hash")).toString();
                if (!hashPattern.match(hash).hasMatch()) {
                    result.failed << QStringLiteral("%1 (not an asset hash)").arg(it.key());
                    continue;
                }
                const QString target = safeJoin(objectsDirectory, QStringLiteral("%1/%2").arg(hash.left(2), hash));
                if (target.isEmpty()) {
                    result.failed << QStringLiteral("%1 (not a path inside the assets directory)").arg(hash);
                    continue;
                }
                if (QFileInfo::exists(target))
                    continue;
                jobs.append({ QStringLiteral("https://resources.download.minecraft.net/%1/%2").arg(hash.left(2), hash), target, hash });
            }
        }
    }

    // 4. one queue for everything that is missing; a repair with nothing to do returns at once
    if (jobs.isEmpty() && result.failed.isEmpty()) {
        result.ok = true;
        result.warning = QStringLiteral("nothing was missing");
        return result;
    }

    const FetchSummary fetched = fetchMany(jobs, 8, [&](int done, int total) {
        report(QStringLiteral("files"), done, total, {});
    });
    result.downloaded += fetched.downloaded;
    result.skipped += fetched.skipped;
    result.failed += fetched.failed;

    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        return result;
    }

    result.ok = result.failed.isEmpty();
    if (!result.ok && result.error.isEmpty())
        result.error = QStringLiteral("%1 file(s) could not be fetched").arg(result.failed.size());
    return result;
}

}  // namespace Komorebi
