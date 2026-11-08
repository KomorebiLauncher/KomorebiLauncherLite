// SPDX-License-Identifier: GPL-3.0-only
#include "core/launch/LaunchService.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include "core/InstanceConfig.h"
#include "core/Paths.h"
#include "core/version/VersionResolver.h"

namespace Komorebi {

PreparedLaunch prepareLaunch(const Settings& settings, const QString& versionId, const QString& gameDirectory)
{
    PreparedLaunch prepared;
    prepared.versionId = versionId;

    if (!isSafeComponent(versionId)) {
        prepared.error = QStringLiteral("%1 is not a version id").arg(versionId);
        return prepared;
    }

    const QString directory = versionDirectory(versionId);
    const ResolvedVersion resolved = resolveVersion(versionsRoot(), versionId);
    if (!resolved.ok()) {
        prepared.error = resolved.error;
        return prepared;
    }
    prepared.baseId = resolved.baseId;

    const InstanceConfig instance = InstanceConfig::load(directory);

    LaunchRequest request;
    request.version = resolved.version;
    request.librariesRoot = librariesRoot();
    request.assetsRoot = assetsRoot();
    request.nativesRoot = QDir(directory).filePath(QStringLiteral("natives"));
    request.gameDirectory = gameDirectory.isEmpty() ? directory : gameDirectory;  // versions are isolated
    // A loader does not ship the game; the jar belongs to the version at the bottom of the chain.
    request.clientJar = QDir(versionDirectory(resolved.baseId)).filePath(resolved.baseId + QStringLiteral(".jar"));
    request.playerName = QStringLiteral("Player");

    // memory: the instance wins over the launcher's setting
    request.maxMemoryMb = instance.memoryMb > 0 ? instance.memoryMb : settings.memoryMb;
    if (settings.lockMemory)
        request.minMemoryMb = request.maxMemoryMb;

    request.javaPath = settings.javaPath;
    request.width = settings.width;
    request.height = settings.height;

    request.extraJvmArguments = Settings::splitArguments(instance.jvmArguments.isEmpty() ? settings.jvmArguments : instance.jvmArguments);
    request.extraGameArguments = Settings::splitArguments(instance.gameArguments.isEmpty() ? settings.gameArguments : instance.gameArguments);
    request.highPerformanceGpu = settings.highPerformanceGpu;
    request.legacyFix = settings.legacyFix;
    request.unsafeAgent = settings.unsafeAgent;

    const QString tempRoot = QDir(request.gameDirectory).filePath(QStringLiteral("tmp"));
    if (QDir().mkpath(tempRoot) || QFileInfo::exists(tempRoot))
        request.tempDirectory = tempRoot;

    // A command the player wants run before the game: run it, and report a failure instead of
    // starting a game that was supposed to depend on it.
    if (!settings.preLaunchCommand.isEmpty()) {
        QProcess process;
        process.setWorkingDirectory(request.gameDirectory);
        // A command, not a program name: it may redirect, use a variable, be a pipeline.
#if defined(Q_OS_WIN)
        // setNativeArguments hands the command to cmd.exe as written: splitting it into arguments
        // would break quoting and redirection.
        process.setProgram(QStringLiteral("cmd.exe"));
        process.setNativeArguments(QStringLiteral("/c ") + settings.preLaunchCommand);
#else
        process.setProgram(QStringLiteral("/bin/sh"));
        process.setNativeArguments(QStringLiteral("-c ") + settings.preLaunchCommand);
#endif
        process.start();
        if (!process.waitForStarted(5000) || !process.waitForFinished(60000)) {
            prepared.error = QStringLiteral("the command before the launch did not finish: %1").arg(settings.preLaunchCommand);
            process.kill();
            return prepared;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            prepared.error = QStringLiteral("the command before the launch failed (%1): %2")
                                 .arg(process.exitCode())
                                 .arg(settings.preLaunchCommand);
            return prepared;
        }
    }

    prepared.plan = buildLaunchPlan(request);
    return prepared;
}

}  // namespace Komorebi
