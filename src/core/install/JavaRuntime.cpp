// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/JavaRuntime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/Paths.h"
#include "core/version/VersionJson.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

/// Mojang's list of every runtime they ship, per platform.
constexpr auto kAllRuntimes =
    "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";

QString platformKey()
{
    const Environment environment = Environment::current();
    if (environment.osName == QLatin1String("windows"))
        return environment.osArch == QLatin1String("arm64") ? QStringLiteral("windows-arm64") : QStringLiteral("windows-x64");
    if (environment.osName == QLatin1String("darwin"))
        return environment.osArch == QLatin1String("arm64") ? QStringLiteral("mac-os-arm64") : QStringLiteral("mac-os");
    return environment.osArch == QLatin1String("x86") ? QStringLiteral("linux-i386") : QStringLiteral("linux");
}

/// "1.8.0_202" is Java 8; "17.0.9" is 17.
int majorOfVersion(const QString& value)
{
    QString cleaned = value;
    if (cleaned.startsWith(QLatin1String("1.")))
        return cleaned.section(QLatin1Char('.'), 1, 1).toInt();
    return cleaned.section(QLatin1Char('.'), 0, 0).toInt();
}

}  // namespace

QString javaBinaryIn(const QString& home)
{
#if defined(Q_OS_WIN)
    return QDir(home).filePath(QStringLiteral("bin/java.exe"));
#elif defined(Q_OS_MAC)
    return QDir(home).filePath(QStringLiteral("Contents/Home/bin/java"));
#else
    return QDir(home).filePath(QStringLiteral("bin/java"));
#endif
}

bool hasJavaRuntime(const QString& dataRoot, int major)
{
    const QDir root(QDir(dataRoot).filePath(QStringLiteral("java")));
    if (!root.exists())
        return false;

    for (const auto& entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString home = root.filePath(entry);
        if (!QFileInfo::exists(javaBinaryIn(home)))
            continue;

        QFile release(QDir(home).filePath(QStringLiteral("release")));
        if (release.open(QIODevice::ReadOnly)) {
            for (const auto& line : QString::fromUtf8(release.readAll()).split(QLatin1Char('\n'))) {
                if (!line.startsWith(QLatin1String("JAVA_VERSION=")))
                    continue;
                const QString value = line.mid(13).remove(QLatin1Char('"')).trimmed();
                if (major <= 0 || majorOfVersion(value) == major)
                    return true;
            }
        }
        // No release file: the runtime is there, which is the best this can say without starting it.
        if (major <= 0)
            return true;
    }
    return false;
}

JavaRuntimeResult installJavaRuntime(const QString& dataRoot, const QString& component, int major,
                                     const std::function<void(const InstallProgress&)>& onProgress)
{
    JavaRuntimeResult result;
    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    if (!isSafeComponent(component)) {
        result.error = QStringLiteral("%1 is not a runtime name").arg(component);
        return result;
    }

    report(QStringLiteral("java"), 0, 0, component);
    const BytesResult all = fetchBytes(QString::fromLatin1(kAllRuntimes));
    if (!all.ok) {
        result.error = QStringLiteral("could not read the runtime list: %1").arg(all.error);
        return result;
    }

    const QJsonObject platforms = QJsonDocument::fromJson(all.data).object();
    const QJsonArray builds = platforms.value(platformKey()).toObject().value(component).toArray();
    if (builds.isEmpty()) {
        result.error = QStringLiteral("Mojang lists no %1 for this platform").arg(component);
        return result;
    }
    const QJsonObject build = builds.first().toObject();

    // The runtime's own manifest: a plain list of files with their hashes.
    const QJsonObject manifestRef = build.value(QStringLiteral("manifest")).toObject();
    const QString manifestUrl = manifestRef.value(QStringLiteral("url")).toString();
    const QString manifestSha1 = manifestRef.value(QStringLiteral("sha1")).toString();
    if (manifestUrl.isEmpty()) {
        result.error = QStringLiteral("the %1 entry has no manifest").arg(component);
        return result;
    }
    const BytesResult manifestBytes = fetchBytes(manifestUrl);
    if (!manifestBytes.ok) {
        result.error = QStringLiteral("could not read the %1 manifest: %2").arg(component, manifestBytes.error);
        return result;
    }
    if (!manifestSha1.isEmpty()
        && QString::fromLatin1(QCryptographicHash::hash(manifestBytes.data, QCryptographicHash::Sha1).toHex()) != manifestSha1) {
        result.error = QStringLiteral("the %1 manifest does not match its hash").arg(component);
        return result;
    }

    const QString home = QDir(QDir(dataRoot).filePath(QStringLiteral("java"))).filePath(component);
    const QJsonObject files = QJsonDocument::fromJson(manifestBytes.data).object().value(QStringLiteral("files")).toObject();

    QList<FetchJob> jobs;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QString path = it.key();
        if (path.contains(QLatin1String("..")))
            continue;
        const QString type = entry.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("directory")) {
            QDir().mkpath(QDir(home).filePath(path));
            continue;
        }
        if (type != QLatin1String("file"))
            continue;
        const QJsonObject raw = entry.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("raw")).toObject();
        const QString url = raw.value(QStringLiteral("url")).toString();
        const QString sha1 = raw.value(QStringLiteral("sha1")).toString();
        if (url.isEmpty())
            continue;
        jobs.append({ url, QDir(home).filePath(path), sha1 });
    }

    const FetchSummary fetched = fetchMany(jobs, 8, [&](int done, int total) {
        report(QStringLiteral("java"), done, total, component);
    });
    result.downloaded = fetched.downloaded;
    result.skipped = fetched.skipped;
    if (!fetched.failed.isEmpty()) {
        result.error = QStringLiteral("%1 runtime file(s) could not be fetched: %2").arg(fetched.failed.size()).arg(fetched.failed.first());
        return result;
    }

    result.executable = javaBinaryIn(home);
    if (!QFileInfo::exists(result.executable)) {
        result.error = QStringLiteral("the %1 runtime has no java binary").arg(component);
        return result;
    }

    result.home = home;
    result.ok = true;
    Q_UNUSED(major);
    return result;
}

}  // namespace Komorebi
