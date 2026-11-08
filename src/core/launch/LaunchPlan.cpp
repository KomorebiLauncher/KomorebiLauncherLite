// SPDX-License-Identifier: GPL-3.0-only
#include "core/launch/LaunchPlan.h"

#include <QDir>
#include "core/Paths.h"
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <cmath>
#include <limits>
#include <QRegularExpression>
#include <QProcess>

namespace Komorebi {

QString LaunchPlan::commandLine() const
{
    QStringList parts;
    parts << QLatin1Char('"') + program + QLatin1Char('"');
    for (const auto& argument : arguments) {
        if (argument.contains(QLatin1Char(' ')))
            parts << QLatin1Char('"') + argument + QLatin1Char('"');
        else
            parts << argument;
    }
    return parts.join(QLatin1Char(' '));
}

namespace {

/// The major version a java wants to be: read from the release file next to it when there is one,
/// otherwise asked of the binary itself.
int javaMajorOf(const QString& javaPath)
{
    const QDir bin = QFileInfo(javaPath).absoluteDir();
    QFile release(QDir(bin.absolutePath()).filePath(QStringLiteral("release")));
    if (release.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(release.readAll()).split(QLatin1Char('\n'));
        for (const auto& line : lines) {
            if (!line.startsWith(QLatin1String("JAVA_VERSION=")))
                continue;
            const QString value = line.mid(13).remove(QLatin1Char('"')).trimmed();
            const QString major = value.section(QLatin1Char('.'), 0, 0);
            bool ok = false;
            const int number = major.toInt(&ok);
            if (ok)
                return number;
        }
    }

    // No release file: ask the binary. This costs a process, so it is the fallback.
    QProcess process;
    process.start(javaPath, { QStringLiteral("-version") });
    if (!process.waitForFinished(5000))
        return 0;
    const QString output = QString::fromLocal8Bit(process.readAllStandardError());
    const QRegularExpression pattern(QStringLiteral("version \"([0-9]+)"));
    const auto match = pattern.match(output);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

}  // namespace

QString findJava(int requiredMajor)
{
    const QString executable = QStringLiteral("java.exe");
    QStringList candidates;

    // 1. an explicit setting wins
    const QString fromEnvironment = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_JAVA"));
    if (!fromEnvironment.isEmpty())
        candidates << fromEnvironment;

    // 2. the launcher's own runtimes, newest first
    const QString runtimes = QDir(dataRoot()).filePath(QStringLiteral("java"));
    const QDir runtimeDir(runtimes);
    if (runtimeDir.exists()) {
        for (const auto& entry : runtimeDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
            const QDir bin(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("bin")));
            if (QFileInfo::exists(bin.filePath(executable)))
                candidates << bin.filePath(executable);
        }
    }

    // 3. JAVA_HOME, then whatever is on PATH
    const QString home = QProcessEnvironment::systemEnvironment().value(QStringLiteral("JAVA_HOME"));
    if (!home.isEmpty())
        candidates << QDir(home).filePath(QStringLiteral("bin/") + executable);
    candidates << QStandardPaths::findExecutable(QStringLiteral("java"));

    // A version of Java that cannot load the game's classes is worse than none: pick the one that
    // matches what the version asks for, and only then the newest available.
    QString best;
    int bestDistance = std::numeric_limits<int>::max();
    for (const auto& candidate : candidates) {
        if (candidate.isEmpty() || !QFileInfo::exists(candidate))
            continue;
        const int major = javaMajorOf(candidate);
        const int distance = requiredMajor <= 0 ? 0 : std::abs(major - requiredMajor);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
            if (distance == 0)
                break;
        }
    }
    return best;
}

namespace {

/// Replaces ${name} everywhere it appears; unknown names are left alone so they show up in a dump.
QString substitute(const QString& value, const QHash<QString, QString>& values)
{
    QString result = value;
    for (auto it = values.begin(); it != values.end(); ++it)
        result.replace(QLatin1String("${") + it.key() + QLatin1Char('}'), it.value());
    return result;
}

}  // namespace

LaunchPlan buildLaunchPlan(const LaunchRequest& request)
{
    LaunchPlan plan;
    const Environment environment = Environment::current();

    plan.workingDirectory = request.gameDirectory;
    plan.program = request.javaPath.isEmpty() ? findJava(request.version.javaMajor) : request.javaPath;
    if (plan.program.isEmpty()) {
        plan.error = QStringLiteral("no Java %1 found").arg(request.version.javaMajor);
        return plan;
    }

    // libraries: the classpath, and the natives that have to be unpacked
    QStringList classpath;
    QStringList nativeJars;
    for (const auto& library : request.version.libraries) {
        if (library.artifact.valid()) {
            const QString path = QDir(request.librariesRoot).filePath(library.artifact.path);
            classpath << path;
            if (!QFileInfo::exists(path))
                plan.missingFiles << path;
        }
        if (library.isNative()) {
            const QString path = QDir(request.librariesRoot).filePath(library.natives.path);
            nativeJars << path;
            if (!QFileInfo::exists(path))
                plan.missingFiles << path;
        }
    }

    // A loader's wrapper -- Forge's, at least -- builds its own module layer, and the game jar in
    // that layer must be the one the wrapper patches. Leaving the vanilla jar on the classpath puts
    // two modules on the same package and the layer fails to resolve.
    const bool wrappedLoader = request.version.mainClass.contains(QLatin1String("forgewrapper"));
    if (!request.clientJar.isEmpty() && !wrappedLoader) {
        classpath << request.clientJar;
        if (!QFileInfo::exists(request.clientJar))
            plan.missingFiles << request.clientJar;
    }

    // The files a loader's installer needs are on the classpath, not part of the game's libraries.
    for (const auto& file : request.version.mavenFiles) {
        if (!file.artifact.valid())
            continue;
        const QString path = QDir(request.librariesRoot).filePath(file.artifact.path);
        if (QFileInfo::exists(path))
            classpath << path;
    }

    QDir().mkpath(request.nativesRoot);

    QHash<QString, QString> values;
    values.insert(QStringLiteral("natives_directory"), request.nativesRoot);
    values.insert(QStringLiteral("launcher_name"), QStringLiteral("Komorebi"));
    values.insert(QStringLiteral("launcher_version"), QStringLiteral("0.1"));
    values.insert(QStringLiteral("classpath"), classpath.join(QLatin1Char(';')));
    values.insert(QStringLiteral("classpath_separator"), QStringLiteral(";"));
    values.insert(QStringLiteral("library_directory"), request.librariesRoot);
    values.insert(QStringLiteral("version_name"), request.version.id);
    values.insert(QStringLiteral("version_type"), request.versionType);
    values.insert(QStringLiteral("game_directory"), request.gameDirectory);
    values.insert(QStringLiteral("assets_root"), request.assetsRoot);
    values.insert(QStringLiteral("assets_index_name"), request.version.assets);
    values.insert(QStringLiteral("auth_player_name"), request.playerName);
    values.insert(QStringLiteral("auth_uuid"), request.uuid);
    values.insert(QStringLiteral("auth_access_token"), request.accessToken);
    values.insert(QStringLiteral("user_type"), QStringLiteral("msa"));
    if (request.width > 0 && request.height > 0) {
        values.insert(QStringLiteral("resolution_width"), QString::number(request.width));
        values.insert(QStringLiteral("resolution_height"), QString::number(request.height));
    }

    QStringList arguments;
    for (const auto& argument : request.version.resolvedJvmArguments(environment))
        arguments << substitute(argument, values);

    // The classpath and the natives are not in the manifest's jvm arguments: they are ours to add.
    if (!arguments.contains(QStringLiteral("-cp")) && !arguments.contains(QStringLiteral("-classpath")))
        arguments << QStringLiteral("-cp") << values.value(QStringLiteral("classpath")) << QStringLiteral("-Djava.library.path=") + request.nativesRoot;

    // The JVM unpacks natives and, for a loader, its installer work into java.io.tmpdir. The system
    // one is not always writable (a locked Temp, a policy, another user's leftovers), so the
    // launcher hands over a directory it owns.
    if (!request.tempDirectory.isEmpty()) {
        QDir().mkpath(request.tempDirectory);
        arguments << QStringLiteral("-Djava.io.tmpdir=") + request.tempDirectory;
    }

    if (wrappedLoader) {
        arguments << QStringLiteral("-Dforgewrapper.librariesDir=") + request.librariesRoot;
        arguments << QStringLiteral("-Dforgewrapper.minecraft=") + request.clientJar;
        if (!request.installerJar.isEmpty())
            arguments << QStringLiteral("-Dforgewrapper.installer=") + request.installerJar;
    }

    arguments << request.version.mainClass;
    for (const auto& argument : request.version.resolvedGameArguments(environment))
        arguments << substitute(argument, values);

    plan.arguments = arguments;
    return plan;
}

}  // namespace Komorebi
