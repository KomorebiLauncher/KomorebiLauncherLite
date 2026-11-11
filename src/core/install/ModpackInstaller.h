// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Installing a modpack from a CurseForge zip.
 *
 *  The zip says which game version and which loader it wants, lists the files it needs by project
 *  and file id, and carries a folder of overrides. Installing is therefore: make sure the game and
 *  the loader are installed, download the listed files, then put the overrides in place. The pack
 *  gets a version of its own, so its mods do not leak into anything else.
 */

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/install/ManualMods.h"
#include "core/install/VersionInstaller.h"

namespace Komorebi {

struct ModpackInfo {
    QString name;
    QString version;
    QString minecraftVersion;
    QString loaderKind;     ///< "forge", "fabric", "quilt", empty for none
    QString loaderVersion;
    QString overridesFolder;
    int fileCount = 0;
};

struct ModpackResult {
    bool ok = false;
    QString versionId;      ///< the version to launch
    QString error;
    int downloaded = 0;
    int skipped = 0;
    QStringList failed;
    /**
     * Files whose author allows no download outside CurseForge. They are not a failure: the pack is
     * installed without them and this list is written beside it, so the player can see what is owed
     * and fetch each file from its page.
     */
    QList<ManualMod> missing;
};

/// Reads the manifest inside a pack without installing anything.
bool readModpack(const QString& zipPath, ModpackInfo* info, QString* error);

/// Installs a Modrinth pack (.mrpack), whose index names the files by url and hash.
ModpackResult installMrpack(const QString& zipPath, bool withAssets,
                             const std::function<void(const InstallProgress&)>& onProgress = {});

/// Installs a pack: game, loader, files, overrides. `onProgress` sees each stage.
ModpackResult installModpack(const QString& zipPath, bool withAssets,
                             const std::function<void(const InstallProgress&)>& onProgress = {});

/**
 * Installs a pack whose format is not known in advance.
 *
 * A Modrinth pack carries `modrinth.index.json`; anything else is read as a CurseForge-style zip.
 * This is what "import a zip" uses, so the player does not have to know which one they downloaded.
 */
ModpackResult installPack(const QString& zipPath, bool withAssets,
                          const std::function<void(const InstallProgress&)>& onProgress = {});

}  // namespace Komorebi
