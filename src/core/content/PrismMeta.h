// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Loader components from the public metadata service Prism Launcher publishes.
 *
 *  A loader there is a small JSON that says what it adds to a version of the game: its libraries,
 *  its main class, and which other components it needs. That is enough to build a version the
 *  launcher can start, without running an installer for loaders that ship this way.
 *
 *  This launcher is not affiliated with Prism Launcher; the service is a public source of data, and
 *  it is credited in NOTICE.md like everything else this project did not write.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Komorebi {

class PrismMeta {
   public:
    /// The versions of a component, newest first. With a game version given, only the ones that
    /// component says are for that game.
    static QStringList versions(const QString& uid, const QString& mcVersion, QString* error);

    /// Downloads one component JSON into `<data>/meta/<uid>/<version>.json`.
    static bool fetch(const QString& uid, const QString& version, QString* error);

    /**
     * Builds a local version out of components: the game, the loader, and whatever they require.
     *
     * The merged description is written where the launcher keeps its versions, so starting it is the
     * same as starting any other version.
     */
    static QString install(const QString& loaderUid, const QString& mcVersion, const QString& loaderVersion,
                           QString* versionId, QString* error);
};

}  // namespace Komorebi
