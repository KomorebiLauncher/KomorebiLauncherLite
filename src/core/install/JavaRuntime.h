// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Java, downloaded the way the game expects to find it.
 *
 *  A version's description names the runtime it was built against (java-runtime-epsilon, and a major
 *  version beside it). Mojang publishes those runtimes as plain file trees, so the launcher can put
 *  one in its own directory and use it -- which matters on a machine where the newest installed Java
 *  is older than the game wants, or where there is none at all.
 */

#pragma once

#include <QString>

#include <functional>

#include "core/install/VersionInstaller.h"

namespace Komorebi {

struct JavaRuntimeResult {
    bool ok = false;
    QString error;
    QString executable;  ///< the java binary inside the installed runtime
    QString home;        ///< the runtime's directory
    int downloaded = 0;
    int skipped = 0;
};

/// True when `<data>/java` already holds a runtime of that major version.
bool hasJavaRuntime(const QString& dataRoot, int major);

/// The java binary inside a runtime directory, empty on a platform that has none.
QString javaBinaryIn(const QString& home);

/**
 * Downloads one of Mojang's runtimes into `<dataRoot>/java/<component>`.
 *
 * `onProgress` reports the files as they are fetched, in the same shape the version installer uses.
 */
JavaRuntimeResult installJavaRuntime(const QString& dataRoot, const QString& component, int major,
                                     const std::function<void(const InstallProgress&)>& onProgress);

}  // namespace Komorebi
