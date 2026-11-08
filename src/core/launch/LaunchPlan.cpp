// SPDX-License-Identifier: GPL-3.0-only
#include "core/launch/LaunchPlan.h"

#include "core/util/Archive.h"

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
            // Before Java 9 the release file did not exist and the numbers looked like 1.8.0_202;
            // the second component is the one people mean by "Java 8".
            const QString major = value.startsWith(QLatin1String("1.")) ? value.section(QLatin1Char('.'), 1, 1)
                                                                        : value.section(QLatin1Char('.'), 0, 0);
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
    const QRegularExpression pattern(QStringLiteral("version \"([0-9]+)(?:\\.([0-9]+))?"));
    const auto match = pattern.match(output);
    if (!match.hasMatch())
        return 0;
    // "1.8.0_202" means 8; "17.0.9" means 17.
    if (match.captured(1) == QLatin1String("1") && !match.captured(2).isEmpty())
        return match.captured(2).toInt();
    return match.captured(1).toInt();
}

}  // namespace

QString findJava(int requiredMajor)
{
    const QString executable = javaExecutableName(Environment::current());
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

/**
 * Replaces ${name} in one pass.
 *
 * Chained replaces depended on the hash order and re-scanned text that had just been substituted:
 * a value that itself contained ${...} would be expanded again. One pass fixes both.
 */
QString substitute(const QString& value, const QHash<QString, QString>& values)
{
    static const QRegularExpression placeholder(QStringLiteral("\\$\\{([A-Za-z0-9_]+)\\}"));
    QString result;
    result.reserve(value.size());
    int last = 0;
    auto matches = placeholder.globalMatch(value);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += value.mid(last, match.capturedStart() - last);
        const auto it = values.constFind(match.captured(1));
        result += it == values.constEnd() ? match.captured(0) : it.value();
        last = match.capturedEnd();
    }
    result += value.mid(last);
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
    // Forge and NeoForge start through their bootstrap launcher, which builds the module layer from
    // a patched client of their own. The vanilla jar on the classpath would be a second module
    // exporting the same package, and the layer refuses to resolve.
    const bool bootstrapLoader = request.version.mainClass.contains(QLatin1String("bootstraplauncher"))
                                 || request.version.mainClass.contains(QLatin1String("modlauncher"));
    if (!request.clientJar.isEmpty() && !wrappedLoader && !bootstrapLoader) {
        classpath << request.clientJar;
        if (!QFileInfo::exists(request.clientJar))
            plan.missingFiles << request.clientJar;
    }

    // The files a loader's installer needs are on disk for it to read, not entries the game loads:
    // putting an installer on the classpath is how a wrapper ends up with two modules claiming one
    // package.
    if (!wrappedLoader) {
        for (const auto& file : request.version.mavenFiles) {
            if (!file.artifact.valid())
                continue;
            const QString path = QDir(request.librariesRoot).filePath(file.artifact.path);
            if (QFileInfo::exists(path))
                classpath << path;
        }
    }

    QDir().mkpath(request.nativesRoot);

    QHash<QString, QString> values;
    values.insert(QStringLiteral("natives_directory"), request.nativesRoot);
    values.insert(QStringLiteral("launcher_name"), QStringLiteral("Komorebi"));
    values.insert(QStringLiteral("launcher_version"), QStringLiteral("0.1"));
    const QString separator = classpathSeparator(environment);
    values.insert(QStringLiteral("classpath"), classpath.join(separator));
    values.insert(QStringLiteral("classpath_separator"), separator);
    values.insert(QStringLiteral("library_directory"), request.librariesRoot);
    values.insert(QStringLiteral("version_name"), request.version.id);
    values.insert(QStringLiteral("version_type"), request.versionType);
    values.insert(QStringLiteral("game_directory"), request.gameDirectory);
    values.insert(QStringLiteral("assets_root"), request.assetsRoot);
    values.insert(QStringLiteral("assets_index_name"), request.version.assets);
    values.insert(QStringLiteral("auth_player_name"), request.playerName);
    // Offline defaults: a version that asks for a field we do not have yet gets a value the game
    // accepts rather than a literal ${...} on its command line.
    values.insert(QStringLiteral("auth_uuid"), request.uuid.isEmpty() ? QStringLiteral("00000000000000000000000000000000") : request.uuid);
    values.insert(QStringLiteral("auth_access_token"), request.accessToken);
    values.insert(QStringLiteral("auth_session"), QStringLiteral("token:0:0"));
    values.insert(QStringLiteral("clientid"), request.clientId);
    values.insert(QStringLiteral("auth_xuid"), request.xuid);
    values.insert(QStringLiteral("user_properties"), QStringLiteral("{}"));
    values.insert(QStringLiteral("user_type"), request.userType.isEmpty() ? QStringLiteral("msa") : request.userType);
    if (request.width > 0 && request.height > 0) {
        values.insert(QStringLiteral("resolution_width"), QString::number(request.width));
        values.insert(QStringLiteral("resolution_height"), QString::number(request.height));
    }

    QStringList arguments;
    for (const auto& argument : request.version.resolvedJvmArguments(environment))
        arguments << substitute(argument, values);

    // The classpath and the natives are not in the manifest's jvm arguments: they are ours to add.
    if (!arguments.contains(QStringLiteral("-cp")) && !arguments.contains(QStringLiteral("-classpath")))
        arguments << QStringLiteral("-cp") << values.value(QStringLiteral("classpath"));
    // The natives path is not part of the classpath decision: a manifest that brings its own -cp
    // still needs to be told where the natives are.
    // Native libraries: LWJGL 3 unpacks what it needs itself, but anything before 1.13 expects the
    // files to be there already, so they are unpacked when the version lists them.
    if (!request.nativesRoot.isEmpty() && (!nativeJars.isEmpty() || request.legacyFix)) {
        QDir().mkpath(request.nativesRoot);
        for (const auto& jar : nativeJars) {
            if (!QFileInfo::exists(jar))
                continue;
            QString error;
            if (!extractArchive(jar, request.nativesRoot, &error) && !error.isEmpty())
                plan.error = QStringLiteral("could not unpack %1: %2").arg(QFileInfo(jar).fileName(), error);
        }
    }

    arguments << QStringLiteral("-Djava.library.path=") + request.nativesRoot;

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

    // The heap the player asked for; the JVM defaults are small for a modded game.
    if (request.maxMemoryMb > 0) {
        arguments << QStringLiteral("-Xmx%1m").arg(request.maxMemoryMb);
        if (request.minMemoryMb > 0)
            arguments << QStringLiteral("-Xms%1m").arg(request.minMemoryMb);
    }

    // Native access: modern Java warns (and a future one refuses) when a library reaches into the
    // system without saying so, which is exactly what LWJGL and the hardware probes do.
    if (request.unsafeAgent && request.version.javaMajor >= 17)
        arguments << QStringLiteral("--enable-native-access=ALL-UNNAMED");

    arguments << request.extraJvmArguments;

    arguments << request.version.mainClass;
    for (const auto& argument : request.version.resolvedGameArguments(environment))
        arguments << substitute(argument, values);

    // Whatever is still ${...} here is a field this launcher does not know how to fill: passing it
    // through would hand the game a literal string and fail somewhere far from the cause.
    static const QRegularExpression unresolved(QStringLiteral("\\$\\{[A-Za-z0-9_]+\\}"));
    QStringList missing;
    for (const auto& argument : arguments) {
        auto it = unresolved.globalMatch(argument);
        while (it.hasNext())
            missing << it.next().captured(0);
    }
    if (!missing.isEmpty()) {
        missing.removeDuplicates();
        plan.error = QStringLiteral("unfilled placeholders: %1").arg(missing.join(QStringLiteral(", ")));
        return plan;
    }

    arguments << request.extraGameArguments;

    plan.arguments = arguments;
    return plan;
}

}  // namespace Komorebi
