// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Loaders. Fabric and Quilt describe themselves as a small file that builds on a version of the
 *  game, so installing one is: ask their metadata service for that file, write it next to the
 *  version it names, and fetch the libraries it adds.
 */

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/install/VersionInstaller.h"

namespace Komorebi {

struct LoaderChoice {
    QString kind;     ///< "fabric", "quilt"
    QString version;  ///< empty: the loader's own latest stable
};

struct LoaderResult {
    bool ok = false;
    QString versionId;   ///< the id of the version that was written
    QString error;
    int downloaded = 0;
    int skipped = 0;
    QStringList failed;
};

/// Installs a loader on top of an already installed version.
LoaderResult installLoader(const QString& dataRoot, const QString& minecraftVersion, const LoaderChoice& choice,
                           const std::function<void(const InstallProgress&)>& onProgress = {});

}  // namespace Komorebi
