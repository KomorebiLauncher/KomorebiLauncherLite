// SPDX-License-Identifier: GPL-3.0-only
#include "core/content/PrismMeta.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/Paths.h"
#include "core/install/VersionInstaller.h"
#include "core/version/ComponentResolver.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

QString base()
{
    return QStringLiteral("https://meta.prismlauncher.org/v1");
}

QString componentPath(const QString& uid, const QString& version)
{
    return QDir(QDir(dataRoot()).filePath(QStringLiteral("meta"))).filePath(QStringLiteral("%1/%2.json").arg(uid, version));
}

QJsonObject readComponent(const QString& uid, const QString& version)
{
    QFile file(componentPath(uid, version));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

}  // namespace

QStringList PrismMeta::versions(const QString& uid, const QString& mcVersion, QString* error)
{
    QStringList versions;

    const QString url = QStringLiteral("%1/%2/index.json").arg(base(), uid);
    const JsonResult result = getJson(url, { { QStringLiteral("User-Agent"), QStringLiteral("KomorebiLite/0.1") } });
    if (!result.ok) {
        *error = QStringLiteral("the metadata service answered %1 for %2").arg(result.status).arg(uid);
        return versions;
    }

    const QJsonObject root = QJsonDocument::fromJson(result.body).object();
    for (const auto& entry : root.value(QStringLiteral("versions")).toArray()) {
        const QJsonObject object = entry.toObject();
        const QString version = object.value(QStringLiteral("version")).toString();
        if (version.isEmpty())
            continue;

        // A loader component says which game it is for; anything else in the index is for a game
        // this install is not. An empty mcVersion means "list everything".
        if (!mcVersion.isEmpty()) {
            bool forThisGame = false;
            for (const auto& requirement : object.value(QStringLiteral("requires")).toArray()) {
                const QJsonObject required = requirement.toObject();
                if (required.value(QStringLiteral("uid")).toString() == QLatin1String("net.minecraft")
                    && required.value(QStringLiteral("equals")).toString() == mcVersion) {
                    forThisGame = true;
                    break;
                }
            }
            if (!forThisGame)
                continue;
        }
        versions.append(version);
    }
    return versions;
}

bool PrismMeta::fetch(const QString& uid, const QString& version, QString* error)
{
    if (!isSafeComponent(uid) || !isSafeComponent(version)) {
        *error = QStringLiteral("%1 %2 is not a component").arg(uid, version);
        return false;
    }

    const QString url = QStringLiteral("%1/%2/%3.json").arg(base(), uid, version);
    const JsonResult result = getJson(url, { { QStringLiteral("User-Agent"), QStringLiteral("KomorebiLite/0.1") } });
    if (!result.ok) {
        *error = QStringLiteral("the metadata service answered %1 for %2 %3").arg(result.status).arg(uid, version);
        return false;
    }

    const QString path = componentPath(uid, version);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(result.body) != result.body.size() || !file.commit()) {
        *error = QStringLiteral("could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString PrismMeta::install(const QString& loaderUid, const QString& mcVersion, const QString& loaderVersion,
                           QString* versionId, QString* error)
{
    if (!isSafeComponent(mcVersion)) {
        *error = QStringLiteral("%1 is not a version id").arg(mcVersion);
        return {};
    }

    // The loader component first: it says which game it is built on.
    QString version = loaderVersion;
    if (version.isEmpty()) {
        const QStringList list = versions(loaderUid, mcVersion, error);
        if (list.isEmpty()) {
            *error = error->isEmpty() ? QStringLiteral("the metadata service lists no %1 for %2").arg(loaderUid, mcVersion) : *error;
            return {};
        }
        version = list.first();
    }

    if (!fetch(loaderUid, version, error))
        return {};

    const QJsonObject loader = readComponent(loaderUid, version);
    if (loader.isEmpty()) {
        *error = QStringLiteral("the %1 component came back empty").arg(loaderUid);
        return {};
    }

    // The version this builds on has to exist: inheritance is what gives the loader the game's
    // libraries, its asset index and its arguments.
    const QString baseJson = QDir(dataRoot()).filePath(QStringLiteral("versions/%1/%1.json").arg(mcVersion));
    if (!QFileInfo::exists(baseJson)) {
        VersionInstaller installer(dataRoot());
        const InstallResult game = installer.install(mcVersion, true);
        if (!game.ok) {
            *error = QStringLiteral("Minecraft %1 could not be installed first: %2")
                         .arg(mcVersion, game.failed.isEmpty() ? game.error : game.failed.first());
            return {};
        }
    }

    // A loader component does not necessarily name the game it goes on: some only gate themselves on
    // another component (Fabric says "net.fabricmc.intermediary" and nothing more), and the game is
    // chosen by the player. So the game is always part of the set, plus whatever else the loader pins
    // to an explicit version.
    QList<ComponentRef> components;
    components.append({ QStringLiteral("net.minecraft"), mcVersion });

    for (const auto& entry : loader.value(QStringLiteral("requires")).toArray()) {
        const auto required = entry.toObject();
        ComponentRef ref;
        ref.uid = required.value(QStringLiteral("uid")).toString();
        ref.version = required.value(QStringLiteral("equals")).toString();
        if (ref.uid.isEmpty())
            continue;
        // The game comes from the player's choice, not from the pin.
        if (ref.uid == QLatin1String("net.minecraft"))
            continue;
        // A requirement without a version is a gate, not a component to install.
        if (ref.version.isEmpty())
            continue;
        components.append(ref);
    }

    components.append({ loaderUid, version });

    QStringList missing;
    for (const auto& component : components) {
        if (!fetch(component.uid, component.version, error))
            missing << QStringLiteral("%1 %2").arg(component.uid, component.version);
    }
    if (!missing.isEmpty()) {
        *error = QStringLiteral("could not fetch %1").arg(missing.join(QStringLiteral(", ")));
        return {};
    }

    // Merge, bottom first: the component order does that, and the game has to be at the bottom.
    const VersionJson merged = mergeComponents(QDir(dataRoot()).filePath(QStringLiteral("meta")), components);
    if (merged.id.isEmpty() || merged.mainClass.isEmpty()) {
        *error = QStringLiteral("the components did not describe a version");
        return {};
    }

    // Name it the way a player reads it: the game, then the loader, then its version.
    QString family = loaderUid;
    if (family.contains(QLatin1String("neoforged")))
        family = QStringLiteral("neoforge");
    else if (family.contains(QLatin1String("forge")))
        family = QStringLiteral("forge");
    else if (family.contains(QLatin1String("fabric-loader")))
        family = QStringLiteral("fabric");
    else if (family.contains(QLatin1String("quilt")))
        family = QStringLiteral("quilt");
    QString id = QStringLiteral("%1-%2-%3").arg(mcVersion, family, version);
    id.remove(QLatin1Char(' '));
    if (!isSafeComponent(id)) {
        *error = QStringLiteral("the loader names a version this launcher cannot keep: %1").arg(id);
        return {};
    }

    // The loader component carries only its own libraries and its main class; the game's libraries,
    // arguments and asset index belong to the vanilla version, and inheritance is how this launcher
    // already hands them over.
    QJsonObject out;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("inheritsFrom"), mcVersion);
    out.insert(QStringLiteral("type"), QStringLiteral("release"));
    out.insert(QStringLiteral("mainClass"), merged.mainClass);

    QJsonObject arguments;
    QJsonArray jvm;
    for (const auto& argument : merged.jvmArguments)
        jvm.append(argument.value);
    if (!jvm.isEmpty())
        arguments.insert(QStringLiteral("jvm"), jvm);
    if (!merged.legacyArguments.isEmpty()) {
        // Prism describes the game arguments as the old single string; that string already carries
        // the loader's own additions (--fml.* and the like), so it is what the launch path has to see.
        out.insert(QStringLiteral("minecraftArguments"), merged.legacyArguments.join(QLatin1Char(' ')));
    } else {
        QJsonArray game;
        for (const auto& argument : merged.gameArguments)
            game.append(argument.value);
        if (!game.isEmpty())
            arguments.insert(QStringLiteral("game"), game);
    }
    if (!arguments.isEmpty())
        out.insert(QStringLiteral("arguments"), arguments);

    QJsonArray libraries;
    for (const auto& entry : loader.value(QStringLiteral("libraries")).toArray())
        libraries.append(entry);
    out.insert(QStringLiteral("libraries"), libraries);

    // Files the loader's own wrapper needs beside the game -- the installer jar it patches with, and
    // what that installer's steps read. They are not classpath entries, so they are listed apart.
    QJsonArray mavenFiles;
    for (const auto& file : merged.mavenFiles) {
        if (!file.artifact.valid())
            continue;
        QJsonObject artifact;
        artifact.insert(QStringLiteral("path"), file.artifact.path);
        if (!file.artifact.url.isEmpty())
            artifact.insert(QStringLiteral("url"), file.artifact.url);
        if (!file.artifact.sha1.isEmpty())
            artifact.insert(QStringLiteral("sha1"), file.artifact.sha1);
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), file.name);
        entry.insert(QStringLiteral("downloads"), QJsonObject{ { QStringLiteral("artifact"), artifact } });
        mavenFiles.append(entry);
    }
    if (!mavenFiles.isEmpty())
        out.insert(QStringLiteral("mavenFiles"), mavenFiles);

    const QString directory = versionDirectory(id);
    QDir().mkpath(directory);
    QSaveFile file(QDir(directory).filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(out).toJson()) < 0 || !file.commit()) {
        *error = QStringLiteral("could not write the version for %1").arg(id);
        return {};
    }
    // The game jar lives with the vanilla component, but the launch path looks for it next to the
    // version being started, so it is copied into place here.
    if (!merged.clientJar.url.isEmpty()) {
        const QString jar = QDir(directory).filePath(id + QStringLiteral(".jar"));
        const FetchResult fetched = fetchFile(merged.clientJar.url, jar, merged.clientJar.sha1);
        if (!fetched.ok) {
            *error = QStringLiteral("could not fetch the game jar: %1").arg(fetched.error);
            return {};
        }
    }

    // The libraries and wrapper files the components name have to be on disk before anything starts.
    QList<FetchJob> jobs;
    for (const auto& library : merged.libraries) {
        if (!library.artifact.valid() || library.artifact.url.isEmpty())
            continue;
        const QString target = QDir(librariesRoot()).filePath(library.artifact.path);
        if (QFileInfo::exists(target))
            continue;
        jobs.append({ library.artifact.url, target, library.artifact.sha1 });
    }
    for (const auto& file : merged.mavenFiles) {
        if (!file.artifact.valid() || file.artifact.url.isEmpty())
            continue;
        const QString target = QDir(librariesRoot()).filePath(file.artifact.path);
        if (QFileInfo::exists(target))
            continue;
        jobs.append({ file.artifact.url, target, file.artifact.sha1 });
    }
    const FetchSummary fetched = fetchMany(jobs, 0);
    if (!fetched.failed.isEmpty()) {
        *error = QStringLiteral("%1 file(s) could not be fetched: %2").arg(fetched.failed.size()).arg(fetched.failed.first());
        return {};
    }

    if (versionId)
        *versionId = id;
    return id;
}

}  // namespace Komorebi
