// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/OptiFineInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QUrl>

#include <algorithm>

#include "core/JavaScan.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kDefaultMirror = "https://bmclapi2.bangbang93.com";
constexpr auto kRunnerClass = "komorebilauncher.optifine.Runner";

QString withoutTrailingSlash(QString value)
{
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}

bool writeBytes(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

/// Where the compiled helper class lives: next to the executable, as the build puts it.
QString runnerDirectory()
{
    const QString overridden = qEnvironmentVariable("KOMOREBI_OPTIFINE_RUNNER");
    if (!overridden.isEmpty() && QFileInfo(overridden).isDir())
        return overridden;

    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("optifine-runner"));
    return QFileInfo(beside).isDir() ? beside : QString();
}

/// A Java that can run the installer. Anything from 8 up will do; the newest found is fine.
QString installerJava(const Settings& settings, QString* error)
{
    if (!settings.javaPath.isEmpty() && QFileInfo::exists(settings.javaPath))
        return settings.javaPath;

    const QList<JavaRuntime> runtimes = JavaScan::find();
    JavaRuntime picked = JavaScan::pick(runtimes, 17);
    if (picked.path.isEmpty())
        picked = JavaScan::pick(runtimes, 8);
    if (picked.path.isEmpty()) {
        *error = QStringLiteral("no Java runtime was found to run the OptiFine installer with");
        return {};
    }
    return picked.path;
}

/// OptiFine's own jar. The mirror is tried first; its payload host is not reachable from every
/// network, so the official site -- which serves the same bytes behind a one-time token -- is the
/// fallback rather than an error.
bool fetchOptiFineJar(const QString& mirrorBase, const OptiFineBuild& build, const QString& target, QString* error)
{
    const QString mirror = withoutTrailingSlash(mirrorBase.isEmpty() ? QString::fromLatin1(kDefaultMirror) : mirrorBase);
    const QString fileName = build.filename.isEmpty()
                                 ? QStringLiteral("OptiFine_%1_%2_%3.jar").arg(build.mcVersion, build.type, build.patch)
                                 : build.filename;
    const QString mirrorUrl = QStringLiteral("%1/optifine/%2/%3/%4").arg(mirror, build.mcVersion, build.type, build.patch);

    const FetchResult direct = fetchFile(mirrorUrl, target, {});
    if (direct.ok)
        return true;

    const QString userAgent = QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    const QString lander = QStringLiteral("https://optifine.net/adloadx?f=") + QString::fromLatin1(QUrl::toPercentEncoding(fileName));
    const QList<QPair<QString, QString>> headers = { { QStringLiteral("User-Agent"), userAgent } };

    const JsonResult page = getJson(lander, headers);
    if (!page.ok) {
        *error = QStringLiteral("%1; the official site answered %2").arg(direct.error).arg(page.status);
        return false;
    }

    static const QRegularExpression link(QStringLiteral("downloadx\\?f=[^\"'&]+&x=[0-9a-f]+"));
    const auto match = link.match(QString::fromUtf8(page.body));
    if (!match.hasMatch()) {
        *error = QStringLiteral("%1; the official site offered no download link").arg(direct.error);
        return false;
    }

    QList<QPair<QString, QString>> downloadHeaders = headers;
    downloadHeaders.append({ QStringLiteral("Referer"), lander });
    const JsonResult download = getJson(QStringLiteral("https://optifine.net/") + match.captured(0), downloadHeaders);
    if (!download.ok || download.body.isEmpty()) {
        *error = QStringLiteral("%1; the official download answered %2").arg(direct.error).arg(download.status);
        return false;
    }
    if (!writeBytes(target, download.body)) {
        *error = QStringLiteral("could not write %1").arg(target);
        return false;
    }
    return true;
}

/// Copies everything the installer left in its scratch libraries into the shared libraries root.
int copyScratchLibraries(const QString& from, const QString& to)
{
    int copied = 0;
    const QDir root(from);
    if (!root.exists())
        return copied;

    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString relative = root.relativeFilePath(source);
        const QString target = QDir(to).filePath(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (QFileInfo::exists(target) && !QFile::remove(target))
            continue;
        if (QFile::copy(source, target))
            ++copied;
    }
    return copied;
}

/// The version the installer wrote: its own directory under versions/, not the game it patched.
QString producedVersionJson(const QString& minecraftDirectory, const QString& mcVersion)
{
    QDir versions(QDir(minecraftDirectory).filePath(QStringLiteral("versions")));
    QString fallback;
    for (const auto& entry : versions.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (entry == mcVersion)
            continue;
        const QString candidate = QDir(versions.filePath(entry)).filePath(entry + QStringLiteral(".json"));
        if (!QFileInfo::exists(candidate))
            continue;
        // The installer names its profile "<game>-OptiFine..."; anything else is a leftover.
        if (entry.startsWith(mcVersion + QStringLiteral("-")))
            return candidate;
        if (fallback.isEmpty())
            fallback = candidate;
    }
    return fallback;
}

}  // namespace

QList<OptiFineBuild> optifineBuilds(const QString& mirrorBase, const QString& mcVersion, QString* error)
{
    QList<OptiFineBuild> builds;

    const QString base = withoutTrailingSlash(mirrorBase.isEmpty() ? QString::fromLatin1(kDefaultMirror) : mirrorBase);
    const BytesResult list = fetchBytes(base + QStringLiteral("/optifine/versionList"));
    if (!list.ok) {
        if (error)
            *error = list.error;
        return builds;
    }

    const QJsonArray entries = QJsonDocument::fromJson(list.data).array();
    for (const auto& entry : entries) {
        const QJsonObject object = entry.toObject();
        if (object.value(QStringLiteral("mcversion")).toString() != mcVersion)
            continue;
        OptiFineBuild build;
        build.mcVersion = mcVersion;
        build.type = object.value(QStringLiteral("type")).toString();
        build.patch = object.value(QStringLiteral("patch")).toString();
        build.filename = QFileInfo(object.value(QStringLiteral("filename")).toString()).fileName();
        if (build.type.isEmpty() || build.patch.isEmpty())
            continue;
        builds.append(build);
    }
    return builds;
}

OptiFineResult installOptiFine(const QString& dataRoot, const QString& mcVersion, const QString& patch,
                               const std::function<void(const InstallProgress&)>& onProgress)
{
    OptiFineResult result;
    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    if (!isSafeComponent(mcVersion)) {
        result.error = QStringLiteral("%1 is not a version id").arg(mcVersion);
        return result;
    }
    // OptiFine patches a game that is already there; it cannot be installed on its own.
    const QString gameJson = QDir(dataRoot).filePath(QStringLiteral("versions/%1/%1.json").arg(mcVersion));
    if (!QFileInfo::exists(gameJson)) {
        result.error = QStringLiteral("Minecraft %1 has to be installed first").arg(mcVersion);
        return result;
    }

    const Settings settings = Settings::load();

    report(QStringLiteral("optifine"), 0, 0, QStringLiteral("looking up builds"));
    QString listError;
    const QList<OptiFineBuild> builds = optifineBuilds(settings.mirrorBase, mcVersion, &listError);
    if (builds.isEmpty()) {
        result.error = QStringLiteral("no OptiFine build was listed for %1: %2").arg(mcVersion, listError);
        return result;
    }

    // Newest release, not a preview: the mirror lists previews and releases interleaved, so the last
    // release in its order is the newest one. A player choosing "OptiFine" means the build they can
    // join a server with.
    OptiFineBuild build = builds.last();
    for (const auto& candidate : builds) {
        const QString name = candidate.filename.toLower();
        if (name.startsWith(QStringLiteral("preview_")) || candidate.patch.startsWith(QStringLiteral("pre"), Qt::CaseInsensitive))
            continue;
        build = candidate;
    }
    if (!patch.isEmpty()) {
        const auto found = std::find_if(builds.begin(), builds.end(), [&patch](const OptiFineBuild& candidate) {
            return candidate.patch.compare(patch, Qt::CaseInsensitive) == 0;
        });
        if (found == builds.end()) {
            result.error = QStringLiteral("the mirror does not list OptiFine %1 for %2").arg(patch, mcVersion);
            return result;
        }
        build = *found;
    }

    // The installer writes tens of megabytes beside the game. The system temp directory is not
    // always usable (and a launcher should not depend on it), so the scratch tree lives in the data
    // directory -- where this launcher is known to be allowed to write -- and goes away later.
    const QString scratchRoot =
        QDir(dataRoot).filePath(QStringLiteral("tmp/optifine-%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!QDir().mkpath(scratchRoot)) {
        result.error = QStringLiteral("could not make a scratch directory at %1").arg(scratchRoot);
        return result;
    }
    struct ScratchGuard {
        QString path;
        ~ScratchGuard() { QDir(path).removeRecursively(); }
    } scratch{ scratchRoot };

    // Layout the installer expects: a throwaway .minecraft, reached through APPDATA so that both
    // the modern doInstall(File) and the old doInstall() find it.
#if defined(Q_OS_WIN)
    const QString appData = QDir(scratchRoot).filePath(QStringLiteral("AppData/Roaming"));
    const QString minecraft = QDir(appData).filePath(QStringLiteral(".minecraft"));
#else
    const QString minecraft = QDir(scratchRoot).filePath(QStringLiteral(".minecraft"));
#endif
    QDir().mkpath(QDir(minecraft).filePath(QStringLiteral("versions")));
    QDir().mkpath(QDir(minecraft).filePath(QStringLiteral("libraries")));
    if (!writeBytes(QDir(minecraft).filePath(QStringLiteral("launcher_profiles.json")),
                    QByteArrayLiteral("{\"profiles\":{},\"settings\":{},\"version\":3}"))) {
        result.error = QStringLiteral("could not prepare the profile file the installer expects");
        return result;
    }

    // The game the installer patches: reuse the copy in this data directory, and only go to the
    // network for what is missing.
    report(QStringLiteral("game"), 0, 0, mcVersion);
    const QString gameDirectory = QDir(minecraft).filePath(QStringLiteral("versions/") + mcVersion);
    QDir().mkpath(gameDirectory);
    QFile::copy(gameJson, QDir(gameDirectory).filePath(mcVersion + QStringLiteral(".json")));

    const QString gameJar = QDir(gameDirectory).filePath(mcVersion + QStringLiteral(".jar"));
    const QString installedJar = QDir(dataRoot).filePath(QStringLiteral("versions/%1/%1.jar").arg(mcVersion));
    if (QFileInfo::exists(installedJar)) {
        QFile::copy(installedJar, gameJar);
    } else {
        QFile jsonFile(QDir(gameDirectory).filePath(mcVersion + QStringLiteral(".json")));
        QString jarUrl;
        QString jarSha1;
        if (jsonFile.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(jsonFile.readAll()).object();
            const QJsonObject client = root.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("client")).toObject();
            jarUrl = client.value(QStringLiteral("url")).toString();
            jarSha1 = client.value(QStringLiteral("sha1")).toString();
        }
        if (jarUrl.isEmpty()) {
            result.error = QStringLiteral("the version metadata for %1 has no client jar").arg(mcVersion);
            return result;
        }
        const FetchResult fetched = fetchFile(jarUrl, gameJar, jarSha1);
        if (!fetched.ok) {
            result.error = QStringLiteral("could not fetch the game jar: %1").arg(fetched.error);
            return result;
        }
    }

    // OptiFine's own jar, from the mirror (or the official site when its payload host is down).
    report(QStringLiteral("optifine"), 0, 0, build.patch);
    const QString fileName = build.filename.isEmpty()
                                 ? QStringLiteral("OptiFine_%1_%2_%3.jar").arg(mcVersion, build.type, build.patch)
                                 : build.filename;
    const QString optifineJar = QDir(scratchRoot).filePath(fileName);
    {
        QString jarError;
        if (!fetchOptiFineJar(settings.mirrorBase, build, optifineJar, &jarError)) {
            result.error = QStringLiteral("could not download OptiFine %1: %2").arg(build.patch, jarError);
            return result;
        }
    }

    const QString runner = runnerDirectory();
    if (runner.isEmpty()) {
        result.error = QStringLiteral("this build has no OptiFine runner (the helper class was not compiled; a JDK is needed at build time)");
        return result;
    }

    QString javaError;
    const QString javaPath = installerJava(settings, &javaError);
    if (javaPath.isEmpty()) {
        result.error = javaError;
        return result;
    }

    // The installer's own window stands in the way; the runner calls the same entry point without it.
    report(QStringLiteral("patching"), 0, 0, build.patch);
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
#if defined(Q_OS_WIN)
    environment.insert(QStringLiteral("APPDATA"), appData);
#else
    environment.insert(QStringLiteral("HOME"), scratchRoot);
#endif
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(minecraft);
    process.setProcessChannelMode(QProcess::MergedChannels);

    const QString classPath = QStringList{ optifineJar, runner }.join(QDir::listSeparator());
    process.start(javaPath, { QStringLiteral("-cp"), classPath, QString::fromLatin1(kRunnerClass), minecraft });
    if (!process.waitForStarted(15000)) {
        result.error = QStringLiteral("the OptiFine installer did not start");
        return result;
    }

    QString output;
    const qint64 deadline = 15 * 60 * 1000;
    QElapsedTimer clock;
    clock.start();
    while (process.state() != QProcess::NotRunning && clock.elapsed() < deadline) {
        process.waitForReadyRead(500);
        output += QString::fromLocal8Bit(process.readAll());
        const QStringList lines = output.split(QLatin1Char('\n'));
        if (!lines.isEmpty())
            report(QStringLiteral("patching"), 0, 0, lines.last().trimmed());
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        result.error = QStringLiteral("the OptiFine installer did not finish in 15 minutes");
        return result;
    }
    output += QString::fromLocal8Bit(process.readAll());

    // Success is judged on what appeared, not on the exit code: this installer has more than one way
    // to end badly after it has already written the version it was asked for.
    const QString produced = producedVersionJson(minecraft, mcVersion);
    if (produced.isEmpty()) {
        result.error = QStringLiteral("the OptiFine installer produced no version: %1").arg(output.right(400).trimmed());
        return result;
    }

    QFile producedFile(produced);
    if (!producedFile.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("could not read the version the OptiFine installer wrote");
        return result;
    }
    QJsonObject version = QJsonDocument::fromJson(producedFile.readAll()).object();
    producedFile.close();

    QString id = version.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = QFileInfo(produced).completeBaseName();
    if (!isSafeComponent(id)) {
        result.error = QStringLiteral("the OptiFine installer produced an unusable version id: %1").arg(id);
        return result;
    }

    // The jars OptiFine writes exist nowhere public, so everything the installer unpacked moves into
    // the shared libraries root, where the maven-shaped paths in the version file point.
    report(QStringLiteral("libraries"), 0, 0, id);
    result.downloaded = copyScratchLibraries(QDir(minecraft).filePath(QStringLiteral("libraries")),
                                             QDir(dataRoot).filePath(QStringLiteral("libraries")));

    version.insert(QStringLiteral("id"), id);
    const QString directory = QDir(dataRoot).filePath(QStringLiteral("versions/") + id);
    QDir().mkpath(directory);
    if (!writeBytes(QDir(directory).filePath(id + QStringLiteral(".json")), QJsonDocument(version).toJson(QJsonDocument::Indented))) {
        result.error = QStringLiteral("could not write %1").arg(id);
        return result;
    }

    result.ok = true;
    result.versionId = id;
    return result;
}

}  // namespace Komorebi
