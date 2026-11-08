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
#include "core/install/ForgeInstaller.h"
#include "core/install/LoaderInstaller.h"
#include "core/util/Archive.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

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
    QString base = info.name;
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("-"));
    while (base.contains(QLatin1String("--")))
        base.replace(QLatin1String("--"), QLatin1String("-"));
    return QStringLiteral("%1-%2").arg(base, info.version.isEmpty() ? QStringLiteral("1") : info.version);
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
    const InstallResult game = installer.install(index.minecraftVersion, withAssets);
    if (!game.ok) {
        result.error = QStringLiteral("the game could not be installed: %1").arg(game.failed.isEmpty() ? game.error : game.failed.first());
        cleanup();
        return result;
    }

    // 2. the loader the index names
    QString baseVersion = index.minecraftVersion;
    if (index.loaderKind == QLatin1String("forge") || index.loaderKind == QLatin1String("neoforge")) {
        const auto family = index.loaderKind == QLatin1String("forge") ? LoaderFamily::Forge : LoaderFamily::NeoForge;
        const ForgeInstallResult loader = installLoader(family, dataRoot(), index.minecraftVersion, index.loaderVersion, onProgress);
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

    // 4. every file the index lists, at the path it gives
    int done = 0;
    for (const auto& entry : index.files) {
        const auto file = entry.toObject();
        const QString path = file.value(QStringLiteral("path")).toString();
        const auto downloads = file.value(QStringLiteral("downloads")).toArray();
        if (path.isEmpty() || downloads.isEmpty())
            continue;

        report(QStringLiteral("files"), done++, index.files.size(), path);
        const QString target = QDir(packDirectory).filePath(path);
        if (QFileInfo::exists(target)) {
            ++result.skipped;
            continue;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!fetchFile(downloads.first().toString(), target, {}).ok) {
            result.failed << path;
            continue;
        }
        ++result.downloaded;
    }

    // 5. overrides, which the format keeps in a folder of that name
    const QString overrides = QDir(tempPath).filePath(QStringLiteral("overrides"));
    if (QFileInfo::exists(overrides)) {
        report(QStringLiteral("overrides"), 0, 0, QStringLiteral("overrides"));
        copyTree(overrides, packDirectory);
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
    const InstallResult game = installer.install(info.minecraftVersion, withAssets);
    if (!game.ok) {
        result.error = QStringLiteral("the game could not be installed: %1").arg(game.failed.isEmpty() ? game.error : game.failed.first());
        cleanup();
        return result;
    }

    // 2. the loader it wants, which is what the pack's mods were built against
    QString baseVersion = info.minecraftVersion;
    if (info.loaderKind == QLatin1String("forge")) {
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
        const int projectId = file.value(QStringLiteral("projectID")).toInt();
        const int fileId = file.value(QStringLiteral("fileID")).toInt();
        if (projectId <= 0 || fileId <= 0)
            continue;

        report(QStringLiteral("mods"), done++, files.size(), QString::number(projectId));
        // The API knows the file name; the pack only carries ids.
        const QList<CurseForgeFile> list = CurseForge::files(projectId, &error);
        QString name;
        for (const auto& candidate : list) {
            if (candidate.id == fileId) {
                name = candidate.fileName;
                break;
            }
        }
        if (name.isEmpty()) {
            result.failed << QStringLiteral("project %1 file %2 (the API did not list it)").arg(projectId).arg(fileId);
            continue;
        }

        const QString target = QDir(modsDirectory).filePath(name);
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
