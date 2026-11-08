// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Forge.
 *
 *  Forge's own installer is the thing that knows how to patch the game: it runs the processors,
 *  rewrites the mappings and writes the version it created. Re-implementing that would be a second
 *  copy of a moving target, so the launcher runs the installer instead -- headless, into its own
 *  directory, which is a mode the installer supports on purpose.
 */

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/install/VersionInstaller.h"

namespace Komorebi {

struct ForgeInstallResult {
    bool ok = false;
    QString versionId;   ///< the version directory the installer created
    QString error;
};

/// The Forge versions published for a game version, newest first.
QStringList forgeVersions(const QString& minecraftVersion, QString* error);

/**
 * Installs Forge on top of an installed game version.
 *
 * `forgeVersion` may be empty, in which case the newest published version is used. The installer
 * needs a launcher profile file next to the game, which the launcher writes if it is missing.
 */
ForgeInstallResult installForge(const QString& dataRoot, const QString& minecraftVersion, const QString& forgeVersion,
                                const std::function<void(const InstallProgress&)>& onProgress = {});

}  // namespace Komorebi
