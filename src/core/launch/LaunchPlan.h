// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Turning a version description into the command line that starts the game.
 *
 *  No process is started here: the plan is a value, so it can be printed, compared and checked
 *  without launching anything.
 */

#pragma once

#include <QString>
#include <QStringList>

#include "core/version/VersionJson.h"

namespace Komorebi {

/// Everything a launch needs, resolved: the program, its arguments, and where it runs.
struct LaunchPlan {
    QString program;           ///< the java binary
    QStringList arguments;     ///< the full command line, program excluded
    QString workingDirectory;  ///< the game directory
    QStringList missingFiles;  ///< libraries or natives the plan expects but cannot find
    QString error;             ///< set when the plan cannot be built at all
    QString priority;          ///< the priority class the process should get, when it runs

    bool valid() const { return error.isEmpty() && !program.isEmpty(); }
    QString commandLine() const;
};

struct LaunchRequest {
    VersionJson version;
    QString librariesRoot;   ///< where the library jars live
    QString nativesRoot;     ///< where the native jars are unpacked
    QString assetsRoot;
    QString gameDirectory;
    QString clientJar;       ///< the game jar, added to the end of the classpath
    QString installerJar;    ///< a loader installer, handed to its wrapper as a property
    QString tempDirectory;   ///< where the game may unpack things; empty means the system default
    QString javaPath;        ///< empty: search for one
    QString playerName = QStringLiteral("Player");
    QString uuid;
    QString accessToken = QStringLiteral("0");
    QString clientId;                          ///< the launcher client id, for the versions that ask
    QString xuid;                              ///< Xbox user id, from the account
    QString userType = QStringLiteral("msa");
    QString versionType = QStringLiteral("release");
    int maxMemoryMb = 0;   ///< 0: leave the JVM default alone
    int minMemoryMb = 0;   ///< 0: let the JVM grow; equal to max means locked
    QStringList extraJvmArguments;   ///< from the settings page
    QStringList extraGameArguments;  ///< appended after the manifest arguments
    bool highPerformanceGpu = false;
    bool legacyFix = false;
    bool unsafeAgent = true;
    int width = 0;
    int height = 0;
};

/// Finds a Java executable the version can actually run on.
QString findJava(int requiredMajor);

/// Builds the command line; when something is missing the plan still exists, and says what.
LaunchPlan buildLaunchPlan(const LaunchRequest& request);

}  // namespace Komorebi
