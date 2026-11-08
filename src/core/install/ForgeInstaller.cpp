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

#include <algorithm>

#include "core/JavaScan.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

/// Where a loader lives, and how the version it creates is named.
struct Family {
    const char* metadata;
    const char* maven;
    const char* group;
    const char* artifact;
    const char* prefix;   ///< the version directory starts with this
    bool gamePrefixed;    ///< versions are named "<game>-<loader>", as Forge does
};

constexpr Family kForge{
    "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml",
    "https://maven.minecraftforge.net", "net/minecraftforge", "forge", "forge", true,
};

// NeoForge began as a fork of Forge: for 1.20.1 it publishes net.neoforged:forge, and from 1.20.2
// net.neoforged:neoforge, whose versions are named after the game (20.4.237 for 1.20.4).
constexpr Family kNeoForgeLegacy{
    "https://maven.neoforged.net/releases/net/neoforged/forge/maven-metadata.xml",
    "https://maven.neoforged.net/releases", "net/neoforged", "forge", "neoforge", true,
};

constexpr Family kNeoForge{
    "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml",
    "https://maven.neoforged.net/releases", "net/neoforged", "neoforge", "neoforge", false,
};

/// "1.20.4" is "20.4" in NeoForge's own numbering.
QString neoForgePrefix(const QString& minecraftVersion)
{
    QStringList parts = minecraftVersion.split(QLatin1Char('.'));
    if (!parts.isEmpty() && parts.first() == QLatin1String("1"))
        parts.removeFirst();
    return parts.join(QLatin1Char('.'));
}

const Family& familyOf(LoaderFamily family, const QString& minecraftVersion)
{
    if (family == LoaderFamily::Forge)
        return kForge;
    // NeoForge only exists from 1.20.2 onwards under its own name; 1.20.1 came as net.neoforged:forge.
    return minecraftVersion == QLatin1String("1.20.1") ? kNeoForgeLegacy : kNeoForge;
}

/// The launcher profile file Forge and NeoForge look for before they install anything.
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

QString expectedVersionId(const Family& family, const QString& minecraftVersion, const QString& full)
{
    if (family.gamePrefixed) {
        // "<game>-<loader>-<version>", e.g. 1.20.1-forge-47.4.28
        const QString loaderPart = full.section(QLatin1Char('-'), 1);
        return QStringLiteral("%1-%2-%3").arg(minecraftVersion, QString::fromLatin1(family.prefix), loaderPart);
    }
    return QStringLiteral("%1-%2").arg(QString::fromLatin1(family.prefix), full);
}

QString installedVersionId(const QString& dataRoot, const QString& expected)
{
    const QString path = QDir(dataRoot).filePath(QStringLiteral("versions/%1/%1.json").arg(expected));
    return QFileInfo::exists(path) ? expected : QString();
}

}  // namespace

QStringList loaderVersions(LoaderFamily family, const QString& minecraftVersion, QString* error)
{
    QStringList versions;
    const Family& coordinates = familyOf(family, minecraftVersion);

    const BytesResult bytes = fetchBytes(QString::fromLatin1(coordinates.metadata));
    if (!bytes.ok) {
        if (error)
            *error = bytes.error;
        return versions;
    }

    const QString text = QString::fromUtf8(bytes.data);
    static const QRegularExpression pattern(QStringLiteral("<version>([^<]+)</version>"));
    const QString neoPrefix = neoForgePrefix(minecraftVersion);

    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QString full = matches.next().captured(1);
        if (coordinates.gamePrefixed) {
            if (!full.startsWith(minecraftVersion + QLatin1Char('-')))
                continue;
            versions.append(full.mid(minecraftVersion.size() + 1));
        } else {
            // Named after the game: 20.4.237 belongs to 1.20.4
            if (!full.startsWith(neoPrefix + QLatin1Char('.')))
                continue;
            versions.append(full);
        }
    }

    // Newest first, numerically: a string sort would put 9 after 10.
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

QStringList forgeVersions(const QString& minecraftVersion, QString* error)
{
    return loaderVersions(LoaderFamily::Forge, minecraftVersion, error);
}

ForgeInstallResult installLoader(LoaderFamily family, const QString& dataRoot, const QString& minecraftVersion,
                                 const QString& loaderVersion,
                                 const std::function<void(const InstallProgress&)>& onProgress)
{
    ForgeInstallResult result;

    const auto report = [&onProgress](const QString& stage, int done, int total, const QString& current) {
        if (onProgress)
            onProgress({ stage, done, total, current });
    };

    const Family& coordinates = familyOf(family, minecraftVersion);

    QString version = loaderVersion;
    if (version.isEmpty()) {
        report(QStringLiteral("loader"), 0, 0, {});
        QString error;
        const QStringList versions = loaderVersions(family, minecraftVersion, &error);
        if (versions.isEmpty()) {
            result.error = QStringLiteral("no %1 version for %2: %3")
                               .arg(QString::fromLatin1(coordinates.prefix), minecraftVersion, error);
            return result;
        }
        version = versions.first();
    }

    const QString full = coordinates.gamePrefixed ? QStringLiteral("%1-%2").arg(minecraftVersion, version) : version;
    const QString expected = expectedVersionId(coordinates, minecraftVersion, full);

    // Already installed: nothing to download, nothing to patch again.
    if (const QString existing = installedVersionId(dataRoot, expected); !existing.isEmpty()) {
        result.ok = true;
        result.versionId = existing;
        return result;
    }

    const QString installerRelative = QStringLiteral("%1/%2/%3/%2-%3-installer.jar")
                                          .arg(QString::fromLatin1(coordinates.group),
                                               QString::fromLatin1(coordinates.artifact), full);
    const QString installerPath = QDir(dataRoot).filePath(QStringLiteral("libraries/") + installerRelative);
    const QString installerUrl = QStringLiteral("%1/%2").arg(QString::fromLatin1(coordinates.maven), installerRelative);

    report(QStringLiteral("installer"), 0, 0, full);
    if (!QFileInfo::exists(installerPath)) {
        const FetchResult fetched = fetchFile(installerUrl, installerPath, {});
        if (!fetched.ok) {
            result.error = QStringLiteral("could not download the %1 installer: %2")
                               .arg(QString::fromLatin1(coordinates.prefix), fetched.error);
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
        result.error = QStringLiteral("no Java found to run the installer");
        return result;
    }

    report(QStringLiteral("patching"), 0, 0, full);
    QProcess process;
    process.setWorkingDirectory(QDir(dataRoot).absolutePath());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(javaPath, { QStringLiteral("-jar"), installerPath, QStringLiteral("--installClient"), dataRoot });
    if (!process.waitForStarted(15000)) {
        result.error = QStringLiteral("the installer did not start");
        return result;
    }

    // It patches the game for minutes and says so as it goes; the launcher waits and keeps the end
    // of that for the log.
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
        result.error = QStringLiteral("the installer failed (%1): %2")
                           .arg(process.exitCode())
                           .arg(output.right(400).trimmed());
        return result;
    }

    const QString installed = installedVersionId(dataRoot, expected);
    if (installed.isEmpty()) {
        result.error = QStringLiteral("the installer finished but left no version for %1").arg(expected);
        return result;
    }

    result.ok = true;
    result.versionId = installed;
    return result;
}

ForgeInstallResult installForge(const QString& dataRoot, const QString& minecraftVersion, const QString& forgeVersion,
                                const std::function<void(const InstallProgress&)>& onProgress)
{
    return installLoader(LoaderFamily::Forge, dataRoot, minecraftVersion, forgeVersion, onProgress);
}

}  // namespace Komorebi
