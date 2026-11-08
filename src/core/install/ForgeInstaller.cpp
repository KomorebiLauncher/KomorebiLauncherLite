// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/ForgeInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "core/JavaScan.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kForgeMetadata = "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml";
constexpr auto kForgeMaven = "https://maven.minecraftforge.net";

/// The launcher profile file Forge looks for before it will install anything.
bool ensureLauncherProfiles(const QString& dataRoot)
{
    const QString path = QDir(dataRoot).filePath(QStringLiteral("launcher_profiles.json"));
    if (QFileInfo::exists(path))
        return true;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(R"({"profiles":{},"settings":{},"version":3})");
    return file.commit();
}

/// The version the installer created, read back from the version it wrote.
QString installedVersionId(const QString& dataRoot, const QString& minecraftVersion, const QString& forgeVersion)
{
    const QString expected = QStringLiteral("%1-forge-%2").arg(minecraftVersion, forgeVersion);
    const QString path = QDir(dataRoot).filePath(QStringLiteral("versions/%1/%1.json").arg(expected));
    return QFileInfo::exists(path) ? expected : QString();
}

}  // namespace

QStringList forgeVersions(const QString& minecraftVersion, QString* error)
{
    QStringList versions;

    const BytesResult bytes = fetchBytes(QString::fromLatin1(kForgeMetadata));
    if (!bytes.ok) {
        if (error)
            *error = bytes.error;
        return versions;
    }

    // The metadata lists every combination as "<game>-<forge>"; keep the ones for this game.
    const QString text = QString::fromUtf8(bytes.data);
    static const QRegularExpression pattern(QStringLiteral("<version>([^<]+)</version>"));
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QString full = matches.next().captured(1);
        if (!full.startsWith(minecraftVersion + QLatin1Char('-')))
            continue;
        versions.append(full.mid(minecraftVersion.size() + 1));
    }

    // Newest first: the metadata is chronological, but a string sort is not.
    std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
        const auto parts = [](const QString& value) {
            QList<int> numbers;
            for (const auto& piece : value.split(QLatin1Char('.')))
                numbers.append(piece.toInt());
            return numbers;
        };
        const auto left = parts(a);
        const auto right = parts(b);
        for (int i = 0; i < qMin(left.size(), right.size()); ++i) {
            if (left.at(i) != right.at(i))
                return left.at(i) > right.at(i);
        }
        return left.size() > right.size();
    });
    return versions;
}

ForgeInstallResult installForge(const QString& dataRoot, const QString& minecraftVersion, const QString& forgeVersion,
                                const std::function<void(const InstallProgress&)>& onProgress)
{
    ForgeInstallResult result;

    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    QString version = forgeVersion;
    if (version.isEmpty()) {
        report(QStringLiteral("forge"), 0, 0, {});
        QString error;
        const QStringList versions = forgeVersions(minecraftVersion, &error);
        if (versions.isEmpty()) {
            result.error = QStringLiteral("no Forge version for %1: %2").arg(minecraftVersion, error);
            return result;
        }
        version = versions.first();
    }

    const QString full = QStringLiteral("%1-%2").arg(minecraftVersion, version);
    const QString installerRelative = QStringLiteral("net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(full);
    const QString installerPath = QDir(dataRoot).filePath(QStringLiteral("libraries/") + installerRelative);
    const QString installerUrl = QStringLiteral("%1/%2").arg(QString::fromLatin1(kForgeMaven), installerRelative);

    report(QStringLiteral("installer"), 0, 0, full);
    if (!QFileInfo::exists(installerPath)) {
        const FetchResult fetched = fetchFile(installerUrl, installerPath, {});
        if (!fetched.ok) {
            result.error = QStringLiteral("could not download the Forge installer: %1").arg(fetched.error);
            return result;
        }
    }

    if (!ensureLauncherProfiles(dataRoot)) {
        result.error = QStringLiteral("could not write launcher_profiles.json in %1").arg(QDir::toNativeSeparators(dataRoot));
        return result;
    }

    // The installer needs a Java; any runnable one will do for the installer itself.
    const Settings settings = Settings::load();
    const QList<JavaRuntime> runtimes = JavaScan::find();
    const JavaRuntime java = JavaScan::pick(runtimes, settings.javaPath.isEmpty() ? 17 : 0);
    const QString javaPath = !settings.javaPath.isEmpty() ? settings.javaPath : java.path;
    if (javaPath.isEmpty()) {
        result.error = QStringLiteral("no Java found to run the Forge installer");
        return result;
    }

    report(QStringLiteral("patching"), 0, 0, full);
    QProcess process;
    process.setWorkingDirectory(QDir(dataRoot).absolutePath());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(javaPath, { QStringLiteral("-jar"), installerPath, QStringLiteral("--installClient"), dataRoot });
    if (!process.waitForStarted(15000)) {
        result.error = QStringLiteral("the Forge installer did not start");
        return result;
    }

    // It patches the game for a few minutes and says so as it goes; the launcher waits and keeps
    // the last of it for the log.
    QString output;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(500);
        output += QString::fromLocal8Bit(process.readAll());
        const QStringList lines = output.split(QLatin1Char('\n'));
        if (!lines.isEmpty())
            report(QStringLiteral("patching"), 0, 0, lines.last().trimmed());
    }
    output += QString::fromLocal8Bit(process.readAll());

    if (process.exitCode() != 0) {
        result.error = QStringLiteral("the Forge installer failed (%1): %2")
                           .arg(process.exitCode())
                           .arg(output.right(400).trimmed());
        return result;
    }

    const QString installed = installedVersionId(dataRoot, minecraftVersion, version);
    if (installed.isEmpty()) {
        result.error = QStringLiteral("the installer finished but left no version for %1").arg(full);
        return result;
    }

    result.ok = true;
    result.versionId = installed;
    return result;
}

}  // namespace Komorebi
