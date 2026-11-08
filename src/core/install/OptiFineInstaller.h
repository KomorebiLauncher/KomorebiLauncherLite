// SPDX-License-Identifier: GPL-3.0-only
/*
 *  OptiFine, installed the way OptiFine itself installs: by running its own installer, headlessly.
 *
 *  There is no public description of what OptiFine changes, and no maven repository serves the
 *  jars it writes, so guessing at the patch would drift with every build. Instead the installer jar
 *  is run against a throwaway .minecraft -- with a small helper class standing in for the window it
 *  would otherwise open -- and whatever it produces is copied into this data directory.
 *
 *  The helper class lives in tools/optifinerunner; the build compiles it next to the executable.
 */

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/install/VersionInstaller.h"

namespace Komorebi {

/// One OptiFine build the mirror lists for a game version.
struct OptiFineBuild {
    QString mcVersion;
    QString type;      ///< HD_U, HD_S, ...
    QString patch;     ///< I6, I5, ...
    QString filename;  ///< OptiFine_1.20.1_HD_U_I6.jar
};

struct OptiFineResult {
    bool ok = false;
    QString error;
    QString versionId;
    int downloaded = 0;
};

/// The builds the mirror lists for one game version, oldest first as the mirror returns them.
QList<OptiFineBuild> optifineBuilds(const QString& mirrorBase, const QString& mcVersion, QString* error);

/**
 * Installs OptiFine on top of an already installed game version.
 *
 * `patch` picks a build; empty means the newest release the mirror lists. The version that appears
 * is named by the installer (for example 1.20.1-OptiFine_HD_U_I6) and launches through the same
 * inheritance path as any other loader version.
 */
OptiFineResult installOptiFine(const QString& dataRoot, const QString& mcVersion, const QString& patch,
                               const std::function<void(const InstallProgress&)>& onProgress);

}  // namespace Komorebi
