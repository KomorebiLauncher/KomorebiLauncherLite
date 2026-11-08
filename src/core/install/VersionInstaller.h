// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Installing a version, the way a player expects a launcher to do it: pick a version from the list
 *  Mojang publishes, and let the launcher fetch exactly what that version needs.
 *
 *  The layout is the one the game itself expects, so the folder can be opened with any launcher:
 *
 *      versions/<id>/<id>.json      the description this install was made from
 *      versions/<id>/<id>.jar       the game
 *      versions/<id>/natives/       native libraries, for the versions that need them unpacked
 *      libraries/<group>/<artifact>/<version>/...  shared between versions
 *      assets/indexes/<id>.json     the asset list
 *      assets/objects/<xx>/<hash>   the assets themselves
 */

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/version/VersionJson.h"

namespace Komorebi {

/// What an install is doing, for a progress bar that has to say something.
struct InstallProgress {
    QString stage;    ///< "manifest", "version", "libraries", "assets", ...
    int done = 0;     ///< files finished
    int total = 0;    ///< files known so far
    QString current;  ///< the file being fetched
};

struct InstallResult {
    bool ok = false;
    QString error;
    QStringList failed;   ///< files that could not be fetched, with the reason
    int downloaded = 0;
    int skipped = 0;      ///< already present and correct

    QString summary() const;
};

/// One entry of Mojang's version list.
struct RemoteVersion {
    QString id;
    QString type;   ///< release, snapshot, old_beta, old_alpha
    QString url;    ///< where its description lives
    QString releaseTime;
};

/// The list of installable versions.
class VersionManifest {
public:
    /// Fetches and parses Mojang's manifest. On failure `error` says why and the list is empty.
    static VersionManifest fetch(QString* error);

    QList<RemoteVersion> versions;
    QString latestRelease;
    QString latestSnapshot;

    RemoteVersion find(const QString& id) const;
};

/// Installs one version into a data directory.
class VersionInstaller {
public:
    explicit VersionInstaller(QString dataRoot);

    /// Called for every file; may be empty.
    std::function<void(const InstallProgress&)> onProgress;

    /// Fetches the description, the game, the libraries and the asset index, and then the assets.
    /// `withAssets` off is for a quick install of a version whose assets are already on disk.
    InstallResult install(const QString& versionId, bool withAssets = true);

    /// Where a version's files live.
    QString versionDirectory(const QString& versionId) const;

private:
    InstallResult installFrom(const RemoteVersion& version, bool withAssets);

    QString m_dataRoot;
};

}  // namespace Komorebi
