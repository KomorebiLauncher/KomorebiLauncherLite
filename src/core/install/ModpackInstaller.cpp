// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/ModpackInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/util/Scratch.h"

#include "core/Paths.h"
#include "core/content/CurseForge.h"
#include "core/content/PrismMeta.h"
#include "core/install/ForgeInstaller.h"
#include "core/install/LoaderInstaller.h"
#include "core/util/Archive.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

/// NeoForge through its component metadata: nothing is installed at install time except the files the
/// component lists, and the wrapper it names patches the game on the first launch. Its own installer
/// is the fragile path -- it fetches processor data while it runs and fails where that host is slow.
QString installNeoForgeComponent(const QString& minecraftVersion, const QString& loaderVersion, QString* error)
{
    QString installed;
    const QString id = PrismMeta::install(QStringLiteral("net.neoforged"), minecraftVersion, loaderVersion, &installed, error);
    if (id.isEmpty() && error->isEmpty())
        *error = QStringLiteral("the component metadata did not describe NeoForge %1").arg(loaderVersion);
    return id;
}

QJsonObject readManifest(const QString& zipPath, QString* tempPath, ScratchDirectory** holder, QString* error)
{
    auto* temp = new ScratchDirectory("pack");
    *holder = temp;
    if (!temp->isValid()) {
        *error = QStringLiteral("no place to unpack the pack");
        return {};
    }
    if (!extractArchive(zipPath, temp->path(), error))
        return {};

    const QString manifest = QDir(temp->path()).filePath(QStringLiteral("manifest.json"));
    QFile file(manifest);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("the pack has no manifest.json");
        return {};
    }
    *tempPath = temp->path();
    return QJsonDocument::fromJson(file.readAll()).object();
}

/// The loader entry looks like "forge-47.2.0" or "fabric-0.15.11".
bool parseLoader(const QString& value, QString* kind, QString* version)
{
    const int dash = value.indexOf(QLatin1Char('-'));
    if (dash <= 0)
        return false;
    *kind = value.left(dash);
    *version = value.mid(dash + 1);
    return true;
}

/// Copies a directory tree, which is what a pack's overrides mostly are.
void copyTree(const QString& from, const QString& to)
{
    QDir().mkpath(to);
    const QDir source(from);
    for (const auto& entry : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            copyTree(entry.absoluteFilePath(), target);
        } else {
            QFile::remove(target);
            QFile::copy(entry.absoluteFilePath(), target);
        }
    }
}
QString packVersionId(const ModpackInfo& info)
{
    // Both halves of the id come from the pack's own index, i.e. from whoever wrote the pack, and the
    // result becomes a directory name under versions/. A character that could leave that directory
    // ("/", "\", ":") is replaced, and dots at either end are dropped so no part can be "." or "..".
    const auto sanitise = [](const QString& value, const QString& fallback) {
        QString cleaned = value;
        cleaned.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("-"));
        while (cleaned.contains(QLatin1String("--")))
            cleaned.replace(QLatin1String("--"), QLatin1String("-"));
        while (!cleaned.isEmpty() && (cleaned.startsWith(QLatin1Char('.')) || cleaned.startsWith(QLatin1Char('-'))))
            cleaned.remove(0, 1);
        while (!cleaned.isEmpty() && (cleaned.endsWith(QLatin1Char('.')) || cleaned.endsWith(QLatin1Char('-'))))
            cleaned.chop(1);
        return cleaned.isEmpty() ? fallback : cleaned;
    };
    const QString base = sanitise(info.name, QStringLiteral("pack"));
    const QString version = sanitise(info.version, QStringLiteral("1"));
    const QString id = QStringLiteral("%1-%2").arg(base, version);
    Q_ASSERT(isSafeComponent(id));
    return id;
}

}  // namespace

bool readModpack(const QString& zipPath, ModpackInfo* info, QString* error)
{
    ScratchDirectory* holder = nullptr;
    QString tempPath;
    const QJsonObject manifest = readManifest(zipPath, &tempPath, &holder, error);
    const auto cleanup = [holder] { delete holder; };
    if (manifest.isEmpty()) {
        cleanup();
        return false;
    }

    info->name = manifest.value(QStringLiteral("name")).toString();
    info->version = manifest.value(QStringLiteral("version")).toString();
    info->overridesFolder = manifest.value(QStringLiteral("overrides")).toString(QStringLiteral("overrides"));
    info->fileCount = manifest.value(QStringLiteral("files")).toArray().size();

    const auto minecraft = manifest.value(QStringLiteral("minecraft")).toObject();
    info->minecraftVersion = minecraft.value(QStringLiteral("version")).toString();
    for (const auto& entry : minecraft.value(QStringLiteral("modLoaders")).toArray()) {
        const auto loader = entry.toObject();
        if (!loader.value(QStringLiteral("primary")).toBool(false))
            continue;
        parseLoader(loader.value(QStringLiteral("id")).toString(), &info->loaderKind, &info->loaderVersion);
        break;
    }

    cleanup();
    return !info->minecraftVersion.isEmpty();
}

namespace {

/// A Modrinth pack's index: game, loader, and the files with their own urls.
struct MrpackIndex {
    QString name;
    QString version;
    QString minecraftVersion;
    QString loaderKind;
    QString loaderVersion;
    QJsonArray files;
};

bool readMrpackIndex(const QString& zipPath, MrpackIndex* index, ScratchDirectory** holder, QString* error)
{
    auto* temp = new ScratchDirectory("pack");
    *holder = temp;
    if (!temp->isValid()) {
        *error = QStringLiteral("no place to unpack the pack");
        return false;
    }
    if (!extractArchive(zipPath, temp->path(), error))
        return false;

    QFile file(QDir(temp->path()).filePath(QStringLiteral("modrinth.index.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("the pack has no modrinth.index.json");
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    index->name = root.value(QStringLiteral("name")).toString();
    index->version = root.value(QStringLiteral("versionId")).toString();
    index->files = root.value(QStringLiteral("files")).toArray();

    const QJsonObject dependencies = root.value(QStringLiteral("dependencies")).toObject();
    index->minecraftVersion = dependencies.value(QStringLiteral("minecraft")).toString();
    if (dependencies.contains(QStringLiteral("fabric-loader"))) {
        index->loaderKind = QStringLiteral("fabric");
        index->loaderVersion = dependencies.value(QStringLiteral("fabric-loader")).toString();
    } else if (dependencies.contains(QStringLiteral("quilt-loader"))) {
        index->loaderKind = QStringLiteral("quilt");
        index->loaderVersion = dependencies.value(QStringLiteral("quilt-loader")).toString();
    } else if (dependencies.contains(QStringLiteral("forge"))) {
        index->loaderKind = QStringLiteral("forge");
        index->loaderVersion = dependencies.value(QStringLiteral("forge")).toString();
    } else if (dependencies.contains(QStringLiteral("neoforge"))) {
        index->loaderKind = QStringLiteral("neoforge");
        index->loaderVersion = dependencies.value(QStringLiteral("neoforge")).toString();
    }

    return !index->minecraftVersion.isEmpty();
}

}  // namespace

ModpackResult installMrpack(const QString& zipPath, bool withAssets,
                            const std::function<void(const InstallProgress&)>& onProgress)
{
    ModpackResult result;

    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    ScratchDirectory* holder = nullptr;
    QString error;
    MrpackIndex index;
    if (!readMrpackIndex(zipPath, &index, &holder, &error)) {
        result.error = error;
        delete holder;
        return result;
    }
    const QString tempPath = holder->path();
    const auto cleanup = [holder] { delete holder; };

    // 1. the game
    report(QStringLiteral("game"), 0, 0, index.minecraftVersion);
    VersionInstaller installer(dataRoot());
    installer.onProgress = onProgress;
    const InstallResult game = installer.install(index.minecraftVersion, withAssets);
    if (!game.ok) {
        result.error = QStringLiteral("the game could not be installed: %1").arg(game.failed.isEmpty() ? game.error : game.failed.first());
        cleanup();
        return result;
    }
    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        cleanup();
        return result;
    }

    // 2. the loader the index names
    QString baseVersion = index.minecraftVersion;
    if (index.loaderKind == QLatin1String("neoforge")) {
        // NeoForge comes as component metadata: no installer to run, nothing to download at install
        // time beyond the files the component lists. Its own installer is the fragile path -- it
        // fetches processor data while it runs and fails when neoforged's maven is unreachable.
        QString error;
        const QString installed = installNeoForgeComponent(index.minecraftVersion, index.loaderVersion, &error);
        if (installed.isEmpty()) {
            result.error = QStringLiteral("the loader could not be installed: %1").arg(error);
            cleanup();
            return result;
        }
        baseVersion = installed;
    } else if (index.loaderKind == QLatin1String("forge")) {
        const ForgeInstallResult loader = installLoader(LoaderFamily::Forge, dataRoot(), index.minecraftVersion, index.loaderVersion, onProgress);
        if (!loader.ok) {
            result.error = QStringLiteral("the loader could not be installed: %1").arg(loader.error);
            cleanup();
            return result;
        }
        baseVersion = loader.versionId;
    } else if (!index.loaderKind.isEmpty()) {
        LoaderChoice choice;
        choice.kind = index.loaderKind;
        choice.version = index.loaderVersion;
        const LoaderResult loader = installLoader(dataRoot(), index.minecraftVersion, choice, onProgress);
        if (!loader.ok) {
            result.error = QStringLiteral("the loader could not be installed: %1").arg(loader.error);
            cleanup();
            return result;
        }
        baseVersion = loader.versionId;
    }

    // 3. a version of its own on top of the loader
    ModpackInfo info;
    info.name = index.name;
    info.version = index.version;
    const QString packId = packVersionId(info);
    const QString packDirectory = versionDirectory(packId);
    QDir().mkpath(packDirectory);
    {
        QJsonObject version;
        version.insert(QStringLiteral("id"), packId);
        version.insert(QStringLiteral("inheritsFrom"), baseVersion);
        version.insert(QStringLiteral("type"), QStringLiteral("release"));
        QFile write(QDir(packDirectory).filePath(packId + QStringLiteral(".json")));
        if (!write.open(QIODevice::WriteOnly | QIODevice::Truncate) || write.write(QJsonDocument(version).toJson()) < 0) {
            result.error = QStringLiteral("could not write the pack's version description");
            cleanup();
            return result;
        }
    }

    // 4. every file the index lists, at the path it gives. The urls are in the index, so they are
    // fetched together -- a pack with two hundred mods should not take two hundred round trips.
    QList<FetchJob> jobs;
    for (const auto& entry : index.files) {
        const auto file = entry.toObject();
        const QString path = file.value(QStringLiteral("path")).toString();
        const auto downloads = file.value(QStringLiteral("downloads")).toArray();
        if (path.isEmpty() || downloads.isEmpty())
            continue;

        // The path is written by the pack, so it decides nothing by itself: it has to land inside the
        // pack's own directory. An absolute path, a drive letter or ".." would otherwise write
        // anywhere the player can write, and the install would still say it went well.
        const QString target = safeJoin(packDirectory, path);
        if (target.isEmpty()) {
            result.failed << QStringLiteral("%1 (not a path inside the pack)").arg(path);
            continue;
        }
        if (QFileInfo::exists(target)) {
            ++result.skipped;
            continue;
        }
        const QString sha1 = file.value(QStringLiteral("hashes")).toObject().value(QStringLiteral("sha1")).toString();
        jobs.append({ downloads.first().toString(), target, sha1 });
    }
    const FetchSummary fetched = fetchMany(jobs, 8, [&](int done, int total) {
        report(QStringLiteral("files"), done, total, {});
    });
    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        cleanup();
        return result;
    }
    result.downloaded += fetched.downloaded;
    result.failed += fetched.failed;

    // 5. overrides: `overrides` applies everywhere, `client-overrides` only to the client -- and this
    // launcher only ever installs the client, so a pack that keeps its configs in there lost them.
    for (const QString& folder : { QStringLiteral("overrides"), QStringLiteral("client-overrides") }) {
        const QString source = QDir(tempPath).filePath(folder);
        if (!QFileInfo::exists(source))
            continue;
        report(QStringLiteral("overrides"), 0, 0, folder);
        copyTree(source, packDirectory);
    }

    if (!result.failed.isEmpty()) {
        // A pack missing part of its files starts and then crashes, or looks nothing like the pack.
        // Saying so beats sending the player to the Play page with a broken install.
        result.error = QStringLiteral("%1 of %2 files could not be installed: %3")
                           .arg(result.failed.size())
                           .arg(index.files.size())
                           .arg(result.failed.first());
        cleanup();
        return result;
    }

    cleanup();
    result.versionId = packId;
    result.ok = true;
    return result;
}
ModpackResult installModpack(const QString& zipPath, bool withAssets,
                             const std::function<void(const InstallProgress&)>& onProgress)
{
    ModpackResult result;

    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    ScratchDirectory* holder = nullptr;
    QString tempPath;
    QString error;
    const QJsonObject manifest = readManifest(zipPath, &tempPath, &holder, &error);
    if (manifest.isEmpty()) {
        result.error = error;
        delete holder;
        return result;
    }
    const auto cleanup = [holder] { delete holder; };

    ModpackInfo info;
    info.name = manifest.value(QStringLiteral("name")).toString();
    info.version = manifest.value(QStringLiteral("version")).toString();
    info.overridesFolder = manifest.value(QStringLiteral("overrides")).toString(QStringLiteral("overrides"));
    const auto minecraft = manifest.value(QStringLiteral("minecraft")).toObject();
    info.minecraftVersion = minecraft.value(QStringLiteral("version")).toString();
    for (const auto& entry : minecraft.value(QStringLiteral("modLoaders")).toArray()) {
        const auto loader = entry.toObject();
        if (loader.value(QStringLiteral("primary")).toBool(false)) {
            parseLoader(loader.value(QStringLiteral("id")).toString(), &info.loaderKind, &info.loaderVersion);
            break;
        }
    }

    // 1. the game itself
    report(QStringLiteral("game"), 0, 0, info.minecraftVersion);
    VersionInstaller installer(dataRoot());
    installer.onProgress = onProgress;
    const InstallResult game = installer.install(info.minecraftVersion, withAssets);
    if (!game.ok) {
        result.error = QStringLiteral("the game could not be installed: %1").arg(game.failed.isEmpty() ? game.error : game.failed.first());
        cleanup();
        return result;
    }
    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        cleanup();
        return result;
    }

    // 2. the loader it wants, which is what the pack's mods were built against
    QString baseVersion = info.minecraftVersion;
    if (info.loaderKind == QLatin1String("neoforge")) {
        report(QStringLiteral("neoforge"), 0, 0, info.loaderVersion);
        QString error;
        const QString installed = installNeoForgeComponent(info.minecraftVersion, info.loaderVersion, &error);
        if (installed.isEmpty()) {
            result.error = QStringLiteral("NeoForge could not be installed: %1").arg(error);
            cleanup();
            return result;
        }
        baseVersion = installed;
    } else if (info.loaderKind == QLatin1String("forge")) {
        report(QStringLiteral("forge"), 0, 0, info.loaderVersion);
        const ForgeInstallResult forge = installForge(dataRoot(), info.minecraftVersion, info.loaderVersion, onProgress);
        if (!forge.ok) {
            result.error = QStringLiteral("Forge could not be installed: %1").arg(forge.error);
            cleanup();
            return result;
        }
        baseVersion = forge.versionId;
    } else if (info.loaderKind == QLatin1String("fabric") || info.loaderKind == QLatin1String("quilt")) {
        report(QStringLiteral("loader"), 0, 0, info.loaderVersion);
        LoaderChoice choice;
        choice.kind = info.loaderKind;
        choice.version = info.loaderVersion;
        const LoaderResult loader = installLoader(dataRoot(), info.minecraftVersion, choice, onProgress);
        if (!loader.ok) {
            result.error = QStringLiteral("the loader could not be installed: %1").arg(loader.error);
            cleanup();
            return result;
        }
        baseVersion = loader.versionId;
    }

    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        cleanup();
        return result;
    }

    // 3. the pack is a version of its own, on top of the loader
    const QString packId = packVersionId(info);
    const QString packDirectory = versionDirectory(packId);
    QDir().mkpath(packDirectory);
    {
        QJsonObject version;
        version.insert(QStringLiteral("id"), packId);
        version.insert(QStringLiteral("inheritsFrom"), baseVersion);
        version.insert(QStringLiteral("type"), QStringLiteral("release"));
        QSaveFile file(QDir(packDirectory).filePath(packId + QStringLiteral(".json")));
        if (!file.open(QIODevice::WriteOnly) || !file.commit()) {
            result.error = QStringLiteral("could not write the pack's version description");
            cleanup();
            return result;
        }
        // (the description is rewritten below with the real content; this open is only a check)
        QFile write(QDir(packDirectory).filePath(packId + QStringLiteral(".json")));
        if (write.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            write.write(QJsonDocument(version).toJson());
            write.close();
        }
    }

    // 4. the files the pack lists, into its mods folder
    const QString modsDirectory = QDir(packDirectory).filePath(QStringLiteral("mods"));
    QDir().mkpath(modsDirectory);
    const auto files = manifest.value(QStringLiteral("files")).toArray();
    int done = 0;
    for (const auto& entry : files) {
        const auto file = entry.toObject();
        if (requestCancelled()) {
            result.error = QStringLiteral("cancelled");
            cleanup();
            return result;
        }
        const int projectId = file.value(QStringLiteral("projectID")).toInt();
        const int fileId = file.value(QStringLiteral("fileID")).toInt();
        if (projectId <= 0 || fileId <= 0)
            continue;

        // The API knows the file name; the pack only carries ids.
        const QList<CurseForgeFile> list = CurseForge::files(projectId, &error);
        QString name;
        for (const auto& candidate : list) {
            if (candidate.id == fileId) {
                name = candidate.fileName;
                break;
            }
        }

        report(QStringLiteral("mods"), ++done, files.size(), name.isEmpty() ? QString::number(projectId) : name);
        if (name.isEmpty()) {
            result.failed << QStringLiteral("project %1 file %2 (the API did not list it)").arg(projectId).arg(fileId);
            continue;
        }

        // The file name comes from the API, so it is kept to the mods folder: a name with a
        // separator in it is reduced to its last component, and ".." is refused outright.
        const QString target = safeJoin(modsDirectory, QFileInfo(name).fileName());
        if (target.isEmpty()) {
            result.failed << QStringLiteral("%1 (not a path inside the pack)").arg(name);
            continue;
        }
        if (QFileInfo::exists(target)) {
            ++result.skipped;
            continue;
        }
        if (!CurseForge::downloadFile(projectId, fileId, target, &error)) {
            result.failed << QStringLiteral("%1 (%2)").arg(name, error);
            continue;
        }
        ++result.downloaded;
    }

    // 5. the pack's own files, over the top
    const QString overrides = QDir(tempPath).filePath(info.overridesFolder);
    if (QFileInfo::exists(overrides)) {
        report(QStringLiteral("overrides"), 0, 0, info.overridesFolder);
        const QDir source(overrides);
        for (const auto& entry : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString target = QDir(packDirectory).filePath(entry.fileName());
            if (entry.isDir()) {
                QDir(target).removeRecursively();
                copyTree(entry.absoluteFilePath(), target);
            } else {
                QFile::remove(target);
                QFile::copy(entry.absoluteFilePath(), target);
            }
        }
    }

    cleanup();
    if (!result.failed.isEmpty()) {
        // Same rule as an .mrpack: a pack with files missing is not installed, it is broken.
        result.error = QStringLiteral("%1 of %2 files could not be downloaded: %3")
                           .arg(result.failed.size())
                           .arg(files.size())
                           .arg(result.failed.first());
        return result;
    }
    result.versionId = packId;
    result.ok = true;
    return result;
}

ModpackResult installPack(const QString& zipPath, bool withAssets, const std::function<void(const InstallProgress&)>& onProgress)
{
    // Which kind of pack this is, read from the one file that says so rather than from the name the
    // player downloaded it under.
    ScratchDirectory probe("probe");
    QString error;
    const QString index = QStringLiteral("modrinth.index.json");
    const bool isModrinth = probe.isValid() && extractArchive(zipPath, probe.path(), &error, { index })
                            && QFileInfo::exists(QDir(probe.path()).filePath(index));
    if (isModrinth)
        return installMrpack(zipPath, withAssets, onProgress);
    return installModpack(zipPath, withAssets, onProgress);
}

}  // namespace Komorebi
