// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The launcher, from zero. This file does three things: find the data, load the theme, show the
 *  window. Everything else lives in core (files), kit (controls) and ui (the window).
 */

#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QDebug>
#include <QProcess>
#include <QTimer>

#include "core/Instance.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include <QTranslator>
#include <QLocale>
#include "core/VersionStore.h"
#include "core/launch/LaunchPlan.h"
#include "core/auth/MicrosoftAuth.h"
#include "core/launch/LaunchService.h"
#include "core/util/Archive.h"
#include "core/util/Cleanup.h"
#include "core/util/Scratch.h"
#include "core/content/CurseForge.h"
#include "core/content/PrismMeta.h"
#include "core/content/Modrinth.h"
#include "core/install/ModpackInstaller.h"
#include "core/launch/WindowsTweaks.h"
#include "services/net/Fetcher.h"
#include "services/net/DownloadSite.h"
#include "core/version/ComponentResolver.h"
#include "core/version/VersionJson.h"
#include "core/install/VersionInstaller.h"
#include "core/install/LoaderInstaller.h"
#include "core/install/ForgeInstaller.h"
#include "core/install/OptiFineInstaller.h"
#include "core/version/VersionResolver.h"
#include "dev/Backtrace.h"
#include "dev/SelfTest.h"
#include "theme/Theme.h"
#include "ui/AppWindow.h"
#include "ui/InstallDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/InstanceSettingsDialog.h"
#include "core/InstanceConfig.h"
#include "ui/LoginDialog.h"

namespace {

/**
 * Opens one of the logs a development hook writes beside the build.
 *
 * These hooks are for a developer, so a log that cannot be opened is a nuisance rather than a
 * reason to stop: the failure is said once and the hook goes on, writing into a closed file, which
 * does nothing.
 */
void openHookLog(QFile& file)
{
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qWarning("%s", qPrintable(QStringLiteral("could not write %1").arg(file.fileName())));
}

/**
 * Reads the version a hook was pointed at, by id or by the path to its description.
 *
 * A loader or a pack only records what it adds and points at the version it builds on, so a plan
 * built from that one file is missing the game itself; both forms are resolved through the chain
 * first, which is also what the window does before it launches anything.
 */
Komorebi::ResolvedVersion resolveHookVersion(const QString& idOrPath)
{
    Komorebi::ResolvedVersion resolved;
    QString id = idOrPath;
    if (QFileInfo::exists(idOrPath)) {
        id = Komorebi::VersionJson::fromFile(idOrPath).id;
        if (id.isEmpty()) {
            resolved.error = QStringLiteral("%1 does not name a version").arg(idOrPath);
            return resolved;
        }
    }
    return Komorebi::resolveVersion(Komorebi::versionsRoot(), id);
}

}  // namespace

int main(int argc, char** argv)
{
    Komorebi::installFatalBacktrace();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KomorebiLite"));

    // The language is decided before the first window exists: a translator installed later leaves
    // whatever was already built in the old language.
    {
        const Komorebi::Settings settings = Komorebi::Settings::load();
        QString language = settings.language;
        if (language.isEmpty() || language == QLatin1String("auto"))
            language = QLocale::system().name();
        if (language.startsWith(QLatin1String("zh")))
            language = QStringLiteral("zh_CN");
        else if (language.startsWith(QLatin1String("ja")))
            language = QStringLiteral("ja");
        else
            language.clear();  // English is the source language: no translator needed

        if (!language.isEmpty()) {
            auto* translator = new QTranslator(&app);
            if (translator->load(QStringLiteral(":/i18n/komorebi_%1.qm").arg(language)))
                QApplication::installTranslator(translator);
        }
    }
    QApplication::setOrganizationName(QStringLiteral("KomorebiLite"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/komorebi-lite-256.png")));

    // A theme next to the binary wins, so an edited copy still applies; the same files are compiled
    // in, which is all the single-file build has -- it has no folder to read and used to open black.
    const QString themeDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("theme/light"));
    // What the downloader does for the rest of the run: where files come from, how many at once.
    {
        const Komorebi::Settings settings = Komorebi::Settings::load();
        Komorebi::DownloadPolicy policy;
        if (settings.downloadSite == QLatin1String("mirror-first"))
            policy.mode = Komorebi::MirrorMode::MirrorThenOfficial;
        else if (settings.downloadSite == QLatin1String("mirror-only"))
            policy.mode = Komorebi::MirrorMode::MirrorOnly;
        else if (settings.downloadSite == QLatin1String("official-only"))
            policy.mode = Komorebi::MirrorMode::OfficialOnly;
        else
            policy.mode = Komorebi::MirrorMode::OfficialThenMirror;
        policy.mirrorBase = settings.mirrorBase;
        policy.concurrency = settings.maxConcurrency;
        policy.speedLimitKb = settings.speedLimitKb;
        Komorebi::setDownloadPolicy(policy);
    }

    const Komorebi::Theme theme = Komorebi::Theme::load(themeDir);
    theme.apply(app);

    // While the rewrite is young it reads the instances the installed launcher already has, so the
    // new shell can be compared against the old one on the same data. A packaged build would own
    // its own directory.
    QString instancesRoot = qEnvironmentVariable("KOMOREBI_INSTANCES");
    if (instancesRoot.isEmpty())
        instancesRoot = Komorebi::instancesRoot();

    // The rendering check runs without a window: it renders each control and surface and looks at
    // the pixels, so a black or transparent surface cannot reach a screenshot unnoticed.
    // The rendering check runs without a window and writes its own report; see dev/SelfTest.cpp.
    if (app.arguments().contains(QStringLiteral("--selftest")))
        return Komorebi::Dev::runSelfTest() == 0 ? 0 : 1;
    // Build the command line without starting anything: the plan can be read, compared and checked
    // by whoever is looking, which is how the launch path is developed before it is trusted.
    const QStringList arguments = app.arguments();
    // Installing a version: this is what a player does first, and what the Lite launcher is built
    // around. It writes the standard layout, so the result can be launched by anything.
    const int installIndex = arguments.indexOf(QStringLiteral("--install"));
    if (installIndex >= 0 && installIndex + 1 < arguments.size()) {
        const QString versionId = arguments.at(installIndex + 1);
        const bool withAssets = !arguments.contains(QStringLiteral("--no-assets"));

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("install.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };
        note(QStringLiteral("installing %1 into %2 (assets: %3)")
                 .arg(versionId, Komorebi::dataRoot(), withAssets ? QStringLiteral("yes") : QStringLiteral("no")));

        Komorebi::VersionInstaller installer(Komorebi::dataRoot());
        QString lastStage;
        int lastPercent = -1;
        installer.onProgress = [&](const Komorebi::InstallProgress& progress) {
            const int percent = progress.total > 0 ? progress.done * 100 / progress.total : -1;
            if (progress.stage != lastStage || (percent != lastPercent && percent % 10 == 0)) {
                note(QStringLiteral("%1 %2%3 %4")
                         .arg(progress.stage)
                         .arg(percent >= 0 ? QString::number(percent) + QStringLiteral("%") : QString())
                         .arg(progress.total > 0 ? QStringLiteral(" (%1/%2)").arg(progress.done).arg(progress.total) : QString())
                         .arg(progress.current));
                lastStage = progress.stage;
                lastPercent = percent;
            }
        };

        const Komorebi::InstallResult result = installer.install(versionId, withAssets);
        // summary() already carries the warning, so it is not repeated here.
        note(result.summary());
        for (const auto& failure : result.failed.mid(0, 10))
            note(QStringLiteral("  %1").arg(failure));
        if (!result.ok)
            return 1;

        // A loader is a second step: it builds on the version that was just installed.
        const int loaderIndex = arguments.indexOf(QStringLiteral("--loader"));
        if (loaderIndex >= 0 && loaderIndex + 1 < arguments.size()) {
            const QString spec = arguments.at(loaderIndex + 1);
            if (spec.startsWith(QLatin1String("optifine"))) {
                const QString patch = spec.section(QLatin1Char(':'), 1);
                note(QStringLiteral("installing OptiFine %1 on %2")
                         .arg(patch.isEmpty() ? QStringLiteral("(newest)") : patch, versionId));
                const Komorebi::OptiFineResult optifine = Komorebi::installOptiFine(
                    Komorebi::dataRoot(), versionId, patch,
                    [&note](const Komorebi::InstallProgress& progress) {
                        note(QStringLiteral("%1 %2").arg(progress.stage, progress.current));
                    });
                if (!optifine.ok) {
                    note(QStringLiteral("optifine failed: %1").arg(optifine.error));
                    return 1;
                }
                note(QStringLiteral("optifine installed as %1 (%2 files copied)").arg(optifine.versionId).arg(optifine.downloaded));
                return 0;
            }
        }
        if (loaderIndex >= 0 && loaderIndex + 1 < arguments.size()) {
            const QString loaderSpec = arguments.at(loaderIndex + 1);
            if (loaderSpec.startsWith(QLatin1String("neoforge"))) {
                const QString loaderVersion = loaderSpec.section(QLatin1Char(':'), 1);
                note(QStringLiteral("installing NeoForge %1 on %2")
                         .arg(loaderVersion.isEmpty() ? QStringLiteral("(newest)") : loaderVersion, versionId));
                // NeoForge's component says everything it needs -- its libraries, its main class and
                // the installer its wrapper runs -- so nothing is installed here beyond writing down
                // what the component lists and fetching those files.
                QString error;
                QString installed;
                const QString component =
                    Komorebi::PrismMeta::install(QStringLiteral("net.neoforged"), versionId, loaderVersion, &installed, &error);
                if (component.isEmpty()) {
                    note(QStringLiteral("neoforge failed: %1").arg(error));
                    return 1;
                }
                note(QStringLiteral("neoforge installed as %1").arg(component));
                return 0;
            }
        }
        if (loaderIndex >= 0 && loaderIndex + 1 < arguments.size()) {
            const QString loaderSpec = arguments.at(loaderIndex + 1);
            if (loaderSpec.startsWith(QLatin1String("forge"))) {
                const QString forgeVersion = loaderSpec.section(QLatin1Char(':'), 1);
                note(QStringLiteral("installing Forge %1 on %2")
                         .arg(forgeVersion.isEmpty() ? QStringLiteral("(newest)") : forgeVersion, versionId));
                const Komorebi::ForgeInstallResult forgeResult = Komorebi::installForge(
                    Komorebi::dataRoot(), versionId, forgeVersion,
                    [&note](const Komorebi::InstallProgress& progress) {
                        note(QStringLiteral("%1 %2").arg(progress.stage, progress.current));
                    });
                if (!forgeResult.ok) {
                    note(QStringLiteral("forge failed: %1").arg(forgeResult.error));
                    return 1;
                }
                note(QStringLiteral("forge installed as %1").arg(forgeResult.versionId));
                return 0;
            }
        }
        if (loaderIndex >= 0 && loaderIndex + 1 < arguments.size()) {
            Komorebi::LoaderChoice choice;
            const QString spec = arguments.at(loaderIndex + 1);
            choice.kind = spec.section(QLatin1Char(':'), 0, 0).toLower();
            choice.version = spec.section(QLatin1Char(':'), 1);
            note(QStringLiteral("installing loader %1 %2").arg(choice.kind, choice.version.isEmpty() ? QStringLiteral("(latest)") : choice.version));

            const Komorebi::LoaderResult loaderResult = Komorebi::installLoader(Komorebi::dataRoot(), versionId, choice,
                [&note](const Komorebi::InstallProgress& progress) {
                    note(QStringLiteral("%1 %2/%3 %4").arg(progress.stage).arg(progress.done).arg(progress.total).arg(progress.current));
                });
            if (!loaderResult.ok) {
                note(QStringLiteral("loader failed: %1").arg(loaderResult.error));
                return 1;
            }
            note(QStringLiteral("loader installed as %1 (%2 downloaded, %3 present)")
                     .arg(loaderResult.versionId)
                     .arg(loaderResult.downloaded)
                     .arg(loaderResult.skipped));
        }
        return 0;
    }

    // Launching a version the Lite launcher installed itself: the layout is the standard one, so
    // there is nothing to merge -- the file on disk is the whole description.
    const int versionLaunchIndex = arguments.indexOf(QStringLiteral("--launch-version"));
    if (versionLaunchIndex >= 0 && versionLaunchIndex + 1 < arguments.size()) {
        const QString versionId = arguments.at(versionLaunchIndex + 1);

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launch.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        if (!Komorebi::isSafeComponent(versionId)) {
            note(QStringLiteral("refused: %1 is not a version id").arg(versionId));
            return 1;
        }

        // One place knows how settings, instance overrides and the version come together, so the
        // command line below is exactly what the window would run.
        const int gameDirIndex = arguments.indexOf(QStringLiteral("--game-dir"));
        const QString gameDirectory = gameDirIndex >= 0 && gameDirIndex + 1 < arguments.size()
                                          ? arguments.at(gameDirIndex + 1)
                                          : QString();
        const Komorebi::PreparedLaunch prepared = Komorebi::prepareLaunch(Komorebi::Settings::load(), versionId, gameDirectory);
        if (!prepared.ok()) {
            note(QStringLiteral("cannot launch: %1").arg(prepared.error.isEmpty()
                                                             ? QStringLiteral("%1 file(s) are missing").arg(prepared.plan.missingFiles.size())
                                                             : prepared.error));
            for (const auto& entry : prepared.plan.missingFiles.mid(0, 8))
                note(QStringLiteral("  %1").arg(entry));
            return 1;
        }
        const Komorebi::LaunchPlan plan = prepared.plan;
        note(QStringLiteral("version %1: main %2, java %3").arg(prepared.versionId, plan.program).arg(plan.arguments.value(0)));
        note(QStringLiteral("java: %1").arg(plan.program));
        note(QStringLiteral("missing files: %1").arg(plan.missingFiles.size()));
        note(QStringLiteral("command: %1").arg(plan.commandLine()));
        {
            // One argument per line, next to the log: a command line this long cannot be copied into
            // a shell, and comparing a launch outside the launcher is how environment problems are
            // told apart from launcher problems.
            QFile args(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launch-args.txt")));
            if (args.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                args.write(plan.program.toUtf8() + '\n');
                for (const auto& argument : plan.arguments)
                    args.write(argument.toUtf8() + '\n');
            }
        }
        QProcess process;
        process.setWorkingDirectory(plan.workingDirectory);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(plan.program, plan.arguments);
        if (!process.waitForStarted(15000)) {
            note(QStringLiteral("java did not start"));
            return 1;
        }
        note(QStringLiteral("started, pid %1").arg(process.processId()));
        if (!plan.priority.isEmpty())
            note(QStringLiteral("priority %1: %2").arg(plan.priority, Komorebi::setProcessPriority(process.processId(), plan.priority) ? QStringLiteral("set") : QStringLiteral("refused")));
        note(QStringLiteral("--- output ---"));
        while (process.state() != QProcess::NotRunning) {
            process.waitForReadyRead(1000);
            const QByteArray chunk = process.readAll();
            if (!chunk.isEmpty()) {
                out.write(chunk);
                out.flush();
            }
        }
        out.write(process.readAll());
        out.flush();
        note(QStringLiteral("exited with code %1").arg(process.exitCode()));
        return process.exitCode() == 0 ? 0 : 1;
    }

    // CurseForge: search, or install a pack.
    const int searchPacksIndex = arguments.indexOf(QStringLiteral("--search-packs"));
    if (searchPacksIndex >= 0) {
        const QString query = searchPacksIndex + 1 < arguments.size() ? arguments.at(searchPacksIndex + 1) : QString();
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("packs.log")));
        openHookLog(out);
        QString error;
        const auto packs = Komorebi::CurseForge::searchModpacks(query, 10, &error);
        if (packs.isEmpty()) {
            out.write(QStringLiteral("no packs: %1\n").arg(error).toUtf8());
            return 1;
        }
        for (const auto& pack : packs) {
            out.write(QStringLiteral("%1  %2  (%3 downloads)\n").arg(pack.id).arg(pack.name).arg(pack.downloads).toUtf8());
        }
        return 0;
    }

    // Modrinth: the same two jobs, without a key.
    const int searchModrinthIndex = arguments.indexOf(QStringLiteral("--search-modrinth"));
    if (searchModrinthIndex >= 0) {
        const QString query = searchModrinthIndex + 1 < arguments.size() ? arguments.at(searchModrinthIndex + 1) : QString();
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("packs.log")));
        openHookLog(out);
        QString error;
        const auto packs = Komorebi::Modrinth::searchModpacks(query, 10, &error);
        if (packs.isEmpty()) {
            out.write(QStringLiteral("no packs: %1\n").arg(error).toUtf8());
            return 1;
        }
        for (const auto& pack : packs) {
            out.write(QStringLiteral("%1  %2  (%3 downloads)\n").arg(pack.slug, pack.name).arg(pack.downloads).toUtf8());
        }
        return 0;
    }

    const int installModrinthIndex = arguments.indexOf(QStringLiteral("--install-modrinth"));
    if (installModrinthIndex >= 0 && installModrinthIndex + 1 < arguments.size()) {
        const QString projectId = arguments.at(installModrinthIndex + 1);
        const QString gameVersion = installModrinthIndex + 2 < arguments.size() ? arguments.at(installModrinthIndex + 2) : QString();

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("pack-install.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        QString error;
        const auto versions = Komorebi::Modrinth::versions(projectId, gameVersion, {}, &error);
        if (versions.isEmpty()) {
            note(QStringLiteral("no versions: %1").arg(error));
            return 1;
        }
        Komorebi::CurseForgeFile chosen = versions.first();
        for (const auto& file : versions) {
            if (file.releaseType == QLatin1String("release")) {
                chosen = file;
                break;
            }
        }
        note(QStringLiteral("chosen: %1 (%2)").arg(chosen.version, chosen.fileName));

        Komorebi::ScratchDirectory temp(QStringLiteral("pack"));
        const QString zip = QDir(temp.path()).filePath(QStringLiteral("pack.zip"));        if (!Komorebi::Modrinth::download(chosen.url, zip, &error)) {
            note(QStringLiteral("could not download the pack: %1").arg(error));
            return 1;
        }
        const Komorebi::ModpackResult result = Komorebi::installMrpack(zip, true, [&note](const Komorebi::InstallProgress& progress) {
            note(QStringLiteral("%1 %2/%3 %4").arg(progress.stage).arg(progress.done).arg(progress.total).arg(progress.current));
        });
        if (!result.ok) {
            note(QStringLiteral("failed: %1").arg(result.error));
            return 1;
        }
        note(QStringLiteral("installed as %1 (%2 downloaded, %3 present, %4 failed)")
                 .arg(result.versionId)
                 .arg(result.downloaded)
                 .arg(result.skipped)
                 .arg(result.failed.size()));
        return 0;
    }

    const int downloadPackIndex = arguments.indexOf(QStringLiteral("--download-pack"));
    if (downloadPackIndex >= 0 && downloadPackIndex + 1 < arguments.size()) {
        const int projectId = arguments.at(downloadPackIndex + 1).toInt();
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("packs.log")));
        openHookLog(out);

        QString error;
        const auto files = Komorebi::CurseForge::files(projectId, &error);
        if (files.isEmpty()) {
            out.write(QStringLiteral("no files: %1\n").arg(error).toUtf8());
            return 1;
        }
        // the newest release, or the newest file when the pack has no release
        const Komorebi::CurseForgeFile chosen = files.first();
        out.write(QStringLiteral("chosen: %1 (%2)\n").arg(chosen.displayName, chosen.fileName).toUtf8());

        const QString target = QDir(QCoreApplication::applicationDirPath()).filePath(chosen.fileName);
        if (!Komorebi::CurseForge::downloadFile(projectId, chosen.id, target, &error)) {
            out.write(QStringLiteral("download failed: %1\n").arg(error).toUtf8());
            return 1;
        }
        out.write(QStringLiteral("saved %1\n").arg(target).toUtf8());
        return 0;
    }

    const int modpackIndex = arguments.indexOf(QStringLiteral("--modpack"));
    if (modpackIndex >= 0 && modpackIndex + 1 < arguments.size()) {
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("pack-install.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        const Komorebi::ModpackResult result = Komorebi::installPack(
            arguments.at(modpackIndex + 1), true, [&note](const Komorebi::InstallProgress& progress) {
                note(QStringLiteral("%1 %2/%3 %4").arg(progress.stage).arg(progress.done).arg(progress.total).arg(progress.current));
            });
        if (!result.ok) {
            note(QStringLiteral("failed: %1").arg(result.error));
            return 1;
        }
        note(QStringLiteral("installed as %1 (%2 downloaded, %3 present, %4 failed)")
                 .arg(result.versionId).arg(result.downloaded).arg(result.skipped).arg(result.failed.size()));
        return 0;
    }

    // Development hook: fetch one url through the same layer the installers use, and say what it
    // answered. Answers "why does this file not download" without a debugger.
    const int fetchIndex = arguments.indexOf(QStringLiteral("--fetch"));
    if (fetchIndex >= 0 && fetchIndex + 2 < arguments.size()) {
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fetch.log")));
        openHookLog(out);
        const Komorebi::FetchResult fetched = Komorebi::fetchFile(arguments.at(fetchIndex + 1), arguments.at(fetchIndex + 2), {});
        out.write(QStringLiteral("ok: %1\nbytes: %2\nerror: %3\n").arg(fetched.ok).arg(fetched.bytes).arg(fetched.error).toUtf8());
        return fetched.ok ? 0 : 1;
    }

    // Loaders through component metadata instead of an installer (what Prism's service describes).
    const int componentsIndex = arguments.indexOf(QStringLiteral("--loader-components"));
    if (componentsIndex >= 0 && componentsIndex + 2 < arguments.size()) {
        const QString uid = arguments.at(componentsIndex + 1);
        const QString mcVersion = arguments.at(componentsIndex + 2);
        const QString loaderVersion = componentsIndex + 3 < arguments.size() ? arguments.at(componentsIndex + 3) : QString();

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("components.log")));
        openHookLog(out);
        QString error;
        QString installed;
        const QString id = Komorebi::PrismMeta::install(uid, mcVersion, loaderVersion, &installed, &error);
        if (id.isEmpty()) {
            out.write(QStringLiteral("failed: %1\n").arg(error).toUtf8());
            return 1;
        }
        out.write(QStringLiteral("installed as %1\n").arg(id).toUtf8());
        return 0;
    }

    // A development hook: clean up the junk the game leaves behind, and say how much went.
    if (arguments.contains(QStringLiteral("--clean"))) {
        const qint64 freed = Komorebi::cleanJunk(Komorebi::dataRoot());
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("clean.log")));
        openHookLog(out);
        out.write(QStringLiteral("freed %1 bytes\n").arg(freed).toUtf8());
        return 0;
    }

    // A development hook: unpack an archive the way the launcher does, for checking the unpacker.
    const int unpackIndex = arguments.indexOf(QStringLiteral("--unpack"));
    if (unpackIndex >= 0 && unpackIndex + 2 < arguments.size()) {
        QString error;
        const bool ok = Komorebi::extractArchive(arguments.at(unpackIndex + 1), arguments.at(unpackIndex + 2), &error);
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("unpack.log")));
        openHookLog(out);
        out.write(QStringLiteral("%1: %2\n").arg(ok ? QStringLiteral("ok") : QStringLiteral("failed"), error).toUtf8());
        return ok ? 0 : 1;
    }

    // Completing an installation: the same call the Play page's "Complete files" button makes, so a
    // version whose files went missing can be filled in and checked without opening the window.
    const int repairIndex = arguments.indexOf(QStringLiteral("--repair"));
    if (repairIndex >= 0 && repairIndex + 1 < arguments.size()) {
        const QString versionId = arguments.at(repairIndex + 1);
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("repair.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };
        note(QStringLiteral("repairing %1 in %2").arg(versionId, Komorebi::dataRoot()));

        Komorebi::VersionInstaller installer(Komorebi::dataRoot());
        QString lastStage;
        installer.onProgress = [&](const Komorebi::InstallProgress& progress) {
            if (progress.stage != lastStage) {
                note(QStringLiteral("%1 (%2/%3)").arg(progress.stage).arg(progress.done).arg(progress.total));
                lastStage = progress.stage;
            }
        };

        const Komorebi::InstallResult result = installer.repair(versionId);
        // summary() already carries the warning, so it is not repeated here.
        note(result.summary());
        for (const auto& failure : result.failed.mid(0, 10))
            note(QStringLiteral("  %1").arg(failure));
        return result.ok ? 0 : 1;
    }

    // A development hook: walk an inheritance chain and say what it resolved to.
    const int resolveIndex = arguments.indexOf(QStringLiteral("--resolve"));
    if (resolveIndex >= 0 && resolveIndex + 1 < arguments.size()) {
        const QString id = arguments.at(resolveIndex + 1);
        const Komorebi::ResolvedVersion resolved = Komorebi::resolveVersion(Komorebi::versionsRoot(), id);
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resolve.log")));
        openHookLog(out);
        out.write(QStringLiteral("id: %1\nok: %2\nerror: [%3]\nchain: %4\nbase: %5\nmain: %6\nlibraries: %7\n")
                      .arg(id, resolved.ok() ? QStringLiteral("yes") : QStringLiteral("no"), resolved.error,
                           resolved.chain.join(QLatin1Char(';')), resolved.baseId, resolved.version.mainClass)
                      .arg(resolved.version.libraries.size())
                      .toUtf8());
        return resolved.ok() ? 0 : 1;
    }

    // Signing in: ask for a device code and print what the player has to type.
    const int loginIndex = arguments.indexOf(QStringLiteral("--login"));
    if (loginIndex >= 0) {
        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("login.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        note(QStringLiteral("app id: %1").arg(Komorebi::MicrosoftAuth::clientId()));
        QString error;
        const Komorebi::DeviceCode code = Komorebi::MicrosoftAuth::startDeviceCode(&error);
        if (code.deviceCode.isEmpty()) {
            note(QStringLiteral("no device code: %1").arg(error));
            return 1;
        }
        note(QStringLiteral("open %1 and enter %2").arg(code.verificationUri, code.userCode));
        note(QStringLiteral("polling every %1s, expires in %2s").arg(code.interval).arg(code.expiresIn));

        if (arguments.contains(QStringLiteral("--code-only")))
            return 0;

        Komorebi::MinecraftAccount account;
        if (!Komorebi::MicrosoftAuth::signIn(code, &account, &error)) {
            note(QStringLiteral("sign-in failed: %1").arg(error));
            return 1;
        }
        note(QStringLiteral("signed in as %1 (%2)").arg(account.name, account.uuid));
        return 0;
    }

    const int planIndex = arguments.indexOf(QStringLiteral("--launch-plan"));
    if (planIndex >= 0 && planIndex + 1 < arguments.size()) {
        const Komorebi::ResolvedVersion resolved = resolveHookVersion(arguments.at(planIndex + 1));

        Komorebi::LaunchRequest request;
        request.version = resolved.version;

        request.librariesRoot = Komorebi::librariesRoot();
        request.assetsRoot = Komorebi::assetsRoot();
        request.nativesRoot = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("natives"));
        // the jar belongs to the version at the bottom of the chain, not to the loader
        request.clientJar = resolved.ok()
                                ? QDir(Komorebi::versionDirectory(resolved.baseId)).filePath(resolved.baseId + QStringLiteral(".jar"))
                                : QString();

        const int gameIndex = arguments.indexOf(QStringLiteral("--game-dir"));
        request.gameDirectory = gameIndex >= 0 && gameIndex + 1 < arguments.size()
                                    ? arguments.at(gameIndex + 1)
                                    : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("game"));

        const Komorebi::LaunchPlan plan = Komorebi::buildLaunchPlan(request);

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launchplan.log")));
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QStringList lines;
            lines << QStringLiteral("version: %1 (%2, java %3)").arg(request.version.id, request.version.mainClass)
                         .arg(request.version.javaMajor);
            lines << QStringLiteral("chain: %1, base: %2").arg(resolved.chain.join(QStringLiteral(" -> ")),
                                                              resolved.baseId.isEmpty() ? QStringLiteral("<none>") : resolved.baseId);
            if (!resolved.error.isEmpty())
                lines << QStringLiteral("resolve error: %1").arg(resolved.error);
            lines << QStringLiteral("java: %1").arg(plan.program.isEmpty() ? QStringLiteral("<none>") : plan.program);
            lines << QStringLiteral("libraries: %1, missing: %2").arg(request.version.libraries.size()).arg(plan.missingFiles.size());
            for (const auto& missing : plan.missingFiles.mid(0, 5))
                lines << QStringLiteral("  missing %1").arg(missing);
            if (!plan.error.isEmpty())
                lines << QStringLiteral("error: %1").arg(plan.error);
            lines << QStringLiteral("command:");
            lines << plan.commandLine();
            out.write(lines.join(QLatin1Char('\n')).toUtf8());
        }
        return resolved.ok() && plan.valid() ? 0 : 1;
    }

    // Launch for real: fetch what the version needs, build the plan, start the process and collect
    // its output. This is the development path for M2/M3 and prints everything it did.
    const int launchIndex = arguments.indexOf(QStringLiteral("--launch"));
    if (launchIndex >= 0 && launchIndex + 1 < arguments.size()) {
        const Komorebi::ResolvedVersion resolved = resolveHookVersion(arguments.at(launchIndex + 1));
        const Komorebi::VersionJson version = resolved.version;

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launch.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        note(QStringLiteral("version %1, main %2, java %3").arg(version.id, version.mainClass).arg(version.javaMajor));
        note(QStringLiteral("chain %1, base %2").arg(resolved.chain.join(QStringLiteral(" -> ")), resolved.baseId));
        if (!resolved.error.isEmpty())
            note(QStringLiteral("cannot resolve: %1").arg(resolved.error));

        // a loader does not ship the game: the jar belongs to the version at the bottom of the chain
        const QString clientJar = Komorebi::safeJoin(Komorebi::librariesRoot(),
                                                     QStringLiteral("net/minecraft/client/%1/client-%1.jar").arg(resolved.baseId));

        const int gameDirIndex = arguments.indexOf(QStringLiteral("--game-dir"));
        const QString gameDirectory = gameDirIndex >= 0 && gameDirIndex + 1 < arguments.size()
                                          ? arguments.at(gameDirIndex + 1)
                                          : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("game"));
        QDir().mkpath(gameDirectory);

        Komorebi::LaunchRequest request;
        request.version = version;
        request.librariesRoot = Komorebi::librariesRoot();
        request.assetsRoot = Komorebi::assetsRoot();
        request.nativesRoot = QDir(gameDirectory).filePath(QStringLiteral("natives"));
        request.gameDirectory = gameDirectory;
        request.clientJar = clientJar;
        request.playerName = QStringLiteral("Player");
        // the same settings the window would apply
        {
            const Komorebi::Settings settings = Komorebi::Settings::load();
            request.maxMemoryMb = settings.memoryMb;
            request.javaPath = settings.javaPath;
        }

        // A temp directory next to the game
        // A temp directory next to the game, because that directory is ours: the data directory
        // may belong to another install, and handing the JVM a path that does not exist makes
        // every unpack fail -- which is how loading JNA ends with "access denied".
        const QString tempRoot = QDir(gameDirectory).filePath(QStringLiteral("tmp"));
        if (QDir().mkpath(tempRoot) || QFileInfo::exists(tempRoot))
            request.tempDirectory = tempRoot;
        note(QStringLiteral("temp directory: %1%2").arg(tempRoot, QFileInfo::exists(tempRoot) ? QString() : QStringLiteral(" (could not be created, using the system one)")));

        // the game jar and the asset index are the two files a launch cannot do without
        {
            const QString jar = Komorebi::safeJoin(Komorebi::librariesRoot(),
                                                   QStringLiteral("net/minecraft/client/%1/client-%1.jar").arg(resolved.baseId));
            if (version.clientJar.url.isEmpty()) {
                note(QStringLiteral("client jar: the manifest does not say where it is"));
            } else {
                const Komorebi::FetchResult fetched = Komorebi::fetchFile(version.clientJar.url, jar, version.clientJar.sha1);
                note(QStringLiteral("client jar target: %1").arg(jar));
                note(QStringLiteral("client jar: %1%2").arg(fetched.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                                                             fetched.alreadyPresent ? QStringLiteral(" (cached)") : QString()));
                if (!fetched.ok)
                    note(QStringLiteral("  %1").arg(fetched.error));
            }
            if (version.assetIndex.valid()) {
                const QString indexFile = Komorebi::safeJoin(Komorebi::assetsRoot(), version.assetIndex.path);
                const Komorebi::FetchResult fetched = Komorebi::fetchFile(version.assetIndex.url, indexFile, version.assetIndex.sha1);
                note(QStringLiteral("asset index: %1%2").arg(fetched.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                                                              fetched.alreadyPresent ? QStringLiteral(" (cached)") : QString()));
            }
        }

        // the installer jar, when a loader brought one: ForgeWrapper needs to be told where it is
        for (const auto& file : version.mavenFiles) {
            if (file.name.endsWith(QLatin1String(":installer"))) {
                request.installerJar = Komorebi::safeJoin(Komorebi::librariesRoot(), file.artifact.path);
                break;
            }
        }

        const Komorebi::LaunchPlan plan = Komorebi::buildLaunchPlan(request);
        note(QStringLiteral("java: %1").arg(plan.program));
        note(QStringLiteral("missing: %1").arg(plan.missingFiles.size()));

        if (!plan.valid()) {
            note(QStringLiteral("cannot launch: %1").arg(plan.error));
            return 1;
        }

        QProcess process;
        process.setWorkingDirectory(plan.workingDirectory);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(plan.program, plan.arguments);
        if (!process.waitForStarted(15000)) {
            note(QStringLiteral("java did not start"));
            return 1;
        }
        note(QStringLiteral("started, pid %1").arg(process.processId()));
        if (!plan.priority.isEmpty())
            note(QStringLiteral("priority %1: %2").arg(plan.priority, Komorebi::setProcessPriority(process.processId(), plan.priority) ? QStringLiteral("set") : QStringLiteral("refused")));
        note(QStringLiteral("command: %1").arg(plan.commandLine()));

        // Watch it for a while: a launcher that dies in the first seconds is the failure this check
        // is looking for, so the output is kept either way.
        QString collected;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 40000) {
            process.waitForReadyRead(500);
            collected += QString::fromLocal8Bit(process.readAll());
            if (process.state() == QProcess::NotRunning)
                break;
        }
        const bool stillRunning = process.state() != QProcess::NotRunning;
        if (stillRunning)
            process.kill();
        collected += QString::fromLocal8Bit(process.readAll());
        note(QStringLiteral("--- output ---"));
        note(collected);
        note(stillRunning ? QStringLiteral("still running after 40s: launched") : QStringLiteral("exited on its own"));
        return stillRunning ? 0 : 1;
    }

    // Launch by instance id: the instance names its components, the resolver merges them, and the
    // launch takes it from there. This is the path a real instance will use.
    const int instanceLaunchIndex = arguments.indexOf(QStringLiteral("--launch-instance"));
    if (instanceLaunchIndex >= 0 && instanceLaunchIndex + 1 < arguments.size()) {
        const QString instanceId = arguments.at(instanceLaunchIndex + 1);
        if (!Komorebi::isSafeComponent(instanceId)) {
            QFile bad(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launch.log")));
            if (bad.open(QIODevice::WriteOnly | QIODevice::Truncate))
                bad.write(QStringLiteral("refused: %1 is not a single instance id\n").arg(instanceId).toUtf8());
            return 1;
        }
        const QString instanceDir = QDir(instancesRoot).filePath(instanceId);
        const QString metaRoot = QDir(Komorebi::dataRoot()).filePath(QStringLiteral("meta"));

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launch.log")));
        openHookLog(out);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        const auto components = Komorebi::readComponentList(instanceDir);
        note(QStringLiteral("instance %1: %2 components").arg(instanceId).arg(components.size()));
        for (const auto& component : components)
            note(QStringLiteral("  %1 %2").arg(component.uid, component.version));

        const QStringList missing = Komorebi::missingComponents(metaRoot, components);
        for (const auto& entry : missing)
            note(QStringLiteral("  missing component %1").arg(entry));

        Komorebi::VersionJson version = Komorebi::mergeComponents(metaRoot, components);
        note(QStringLiteral("merged: id %1, main %2, java %3, libraries %4")
                 .arg(version.id, version.mainClass)
                 .arg(version.javaMajor)
                 .arg(version.libraries.size()));

        const int gameDirIndex2 = arguments.indexOf(QStringLiteral("--game-dir"));
        const QString gameDirectory = gameDirIndex2 >= 0 && gameDirIndex2 + 1 < arguments.size()
                                          ? arguments.at(gameDirIndex2 + 1)
                                          : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("game"));
        QDir().mkpath(gameDirectory);

        Komorebi::LaunchRequest request;
        request.version = version;
        request.librariesRoot = Komorebi::librariesRoot();
        request.assetsRoot = Komorebi::assetsRoot();
        request.nativesRoot = QDir(gameDirectory).filePath(QStringLiteral("natives"));
        request.gameDirectory = gameDirectory;
        request.clientJar = Komorebi::safeJoin(Komorebi::librariesRoot(),
                                               QStringLiteral("net/minecraft/client/%1/client-%1.jar").arg(version.id));
        request.playerName = QStringLiteral("Player");
        // the same settings the window would apply
        {
            const Komorebi::Settings settings = Komorebi::Settings::load();
            request.maxMemoryMb = settings.memoryMb;
            request.javaPath = settings.javaPath;
        }

        // A temp directory next to the game
        // A temp directory next to the game, because that directory is ours: the data directory
        // may belong to another install, and handing the JVM a path that does not exist makes
        // every unpack fail -- which is how loading JNA ends with "access denied".
        const QString tempRoot = QDir(gameDirectory).filePath(QStringLiteral("tmp"));
        if (QDir().mkpath(tempRoot) || QFileInfo::exists(tempRoot))
            request.tempDirectory = tempRoot;
        note(QStringLiteral("temp directory: %1%2").arg(tempRoot, QFileInfo::exists(tempRoot) ? QString() : QStringLiteral(" (could not be created, using the system one)")));

        // the game jar and the asset index are the two files a launch cannot do without
        {
            const QString jar = Komorebi::safeJoin(Komorebi::librariesRoot(),
                                                   QStringLiteral("net/minecraft/client/%1/client-%1.jar").arg(version.id));
            if (version.clientJar.url.isEmpty()) {
                note(QStringLiteral("client jar: the manifest does not say where it is"));
            } else {
                const Komorebi::FetchResult fetched = Komorebi::fetchFile(version.clientJar.url, jar, version.clientJar.sha1);
                note(QStringLiteral("client jar target: %1").arg(jar));
                note(QStringLiteral("client jar: %1%2").arg(fetched.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                                                             fetched.alreadyPresent ? QStringLiteral(" (cached)") : QString()));
                if (!fetched.ok)
                    note(QStringLiteral("  %1").arg(fetched.error));
            }
            if (version.assetIndex.valid()) {
                const QString indexFile = Komorebi::safeJoin(Komorebi::assetsRoot(), version.assetIndex.path);
                const Komorebi::FetchResult fetched = Komorebi::fetchFile(version.assetIndex.url, indexFile, version.assetIndex.sha1);
                note(QStringLiteral("asset index: %1%2").arg(fetched.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                                                              fetched.alreadyPresent ? QStringLiteral(" (cached)") : QString()));
            }
        }

        // the installer jar, when a loader brought one: ForgeWrapper needs to be told where it is
        for (const auto& file : version.mavenFiles) {
            if (file.name.endsWith(QLatin1String(":installer"))) {
                request.installerJar = Komorebi::safeJoin(Komorebi::librariesRoot(), file.artifact.path);
                break;
            }
        }

        const Komorebi::LaunchPlan plan = Komorebi::buildLaunchPlan(request);
        note(QStringLiteral("java: %1").arg(plan.program));
        note(QStringLiteral("missing files: %1").arg(plan.missingFiles.size()));
        for (const auto& entry : plan.missingFiles.mid(0, 8))
            note(QStringLiteral("  %1").arg(entry));
        if (!plan.valid()) {
            note(QStringLiteral("cannot launch: %1").arg(plan.error));
            return 1;
        }
        // Missing libraries, natives or the game jar are not a warning: starting like this wastes
        // the player's time with a Java error that says nothing about the real cause.
        if (!plan.missingFiles.isEmpty()) {
            note(QStringLiteral("cannot launch: %1 file(s) missing, first: %2")
                     .arg(plan.missingFiles.size())
                     .arg(plan.missingFiles.first()));
            return 1;
        }

        QProcess process;
        process.setWorkingDirectory(plan.workingDirectory);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(plan.program, plan.arguments);
        if (!process.waitForStarted(15000)) {
            note(QStringLiteral("java did not start"));
            return 1;
        }
        note(QStringLiteral("started, pid %1").arg(process.processId()));
        if (!plan.priority.isEmpty())
            note(QStringLiteral("priority %1: %2").arg(plan.priority, Komorebi::setProcessPriority(process.processId(), plan.priority) ? QStringLiteral("set") : QStringLiteral("refused")));
        note(QStringLiteral("command: %1").arg(plan.commandLine()));

        // A launch by instance is someone playing: the game stays up until it closes, and its
        // output goes to the log as it arrives.
        note(QStringLiteral("--- output ---"));
        while (process.state() != QProcess::NotRunning) {
            process.waitForReadyRead(1000);
            const QByteArray chunk = process.readAll();
            if (!chunk.isEmpty()) {
                out.write(chunk);
                out.flush();
            }
        }
        out.write(process.readAll());
        out.flush();
        note(QStringLiteral("exited with code %1").arg(process.exitCode()));
        return process.exitCode() == 0 ? 0 : 1;
    }

    AppWindow window(theme);

    // A development hook: open a particular page before grabbing the window. It takes a number or the
    // name the rail shows, so a screenshot command can say which page it wants.
    const int pageIndex = arguments.indexOf(QStringLiteral("--page"));
    if (pageIndex >= 0 && pageIndex + 1 < arguments.size()) {
        const QString wanted = arguments.at(pageIndex + 1);
        const QStringList names{ QStringLiteral("play"),   QStringLiteral("modpacks"), QStringLiteral("content"),
                                 QStringLiteral("java"),   QStringLiteral("accounts"), QStringLiteral("logs"),
                                 QStringLiteral("tools"),  QStringLiteral("about") };
        const int byName = names.indexOf(wanted.toLower());
        window.showPage(byName >= 0 ? byName : wanted.toInt());
    }
    window.show();

    // A remembered data directory that has gone looks like a first start, so say it aloud. The
    // automated hooks stand aside: a modal question would hold up an unattended run.
    bool unattended = arguments.contains(QStringLiteral("--selftest")) || arguments.contains(QStringLiteral("--screenshot"))
                      || arguments.contains(QStringLiteral("--install"));
    for (const QString& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--shot-")))
            unattended = true;
    }
    if (!unattended)
        window.warnAboutMissingDataRoot();

    // A development hook: render the window once and quit, so a change can be looked at without a
    // person in the loop. It is not part of the launcher's own behaviour.
    // A development hook for the per-version settings dialog.
    const int shotInstanceIndex = app.arguments().indexOf(QStringLiteral("--shot-instance"));
    if (shotInstanceIndex >= 0) {
        const QString path = shotInstanceIndex + 1 < app.arguments().size() ? app.arguments().at(shotInstanceIndex + 1)
                                                                           : QStringLiteral("instance.png");
        const QString id = shotInstanceIndex + 2 < app.arguments().size() ? app.arguments().at(shotInstanceIndex + 2)
                                                                         : QStringLiteral("1.20.1");
        const Komorebi::InstanceConfig config = Komorebi::InstanceConfig::load(Komorebi::versionDirectory(id));
        auto* dialog = new InstanceSettingsDialog(id, config, Komorebi::Settings::load().memoryMb, &window);
        dialog->show();
        QTimer::singleShot(1500, dialog, [dialog, path] {
            dialog->grab().save(path);
            QApplication::quit();
        });
    }

    // A development hook for the sign-in dialog.
    const int shotLoginIndex = app.arguments().indexOf(QStringLiteral("--shot-login"));
    if (shotLoginIndex >= 0) {
        const QString path = shotLoginIndex + 1 < app.arguments().size() ? app.arguments().at(shotLoginIndex + 1)
                                                                         : QStringLiteral("login.png");
        auto* dialog = new LoginDialog(&window);
        dialog->show();
        QTimer::singleShot(2500, dialog, [dialog, path] {
            dialog->grab().save(path);
            QApplication::quit();
        });
    }

    // A development hook for the settings dialog.
    const int shotSettingsIndex = app.arguments().indexOf(QStringLiteral("--shot-settings"));
    if (shotSettingsIndex >= 0) {
        const QString path = shotSettingsIndex + 1 < app.arguments().size() ? app.arguments().at(shotSettingsIndex + 1)
                                                                            : QStringLiteral("settings.png");
        auto* dialog = new SettingsDialog(Komorebi::Settings::load(), &window);
        dialog->show();
        QTimer::singleShot(1200, dialog, [dialog, path] {
            dialog->grab().save(path);
            QApplication::quit();
        });
    }

    // A development hook for the install dialog: open it, let it fetch the list, then grab it.
    const int shotInstallIndex = app.arguments().indexOf(QStringLiteral("--shot-install"));
    if (shotInstallIndex >= 0) {
        const QString path = shotInstallIndex + 1 < app.arguments().size() ? app.arguments().at(shotInstallIndex + 1)
                                                                           : QStringLiteral("install.png");
        auto* dialog = new InstallDialog(Komorebi::VersionStore(Komorebi::versionsRoot()), &window);
        dialog->show();
        QTimer::singleShot(3000, dialog, [dialog, path] {
            dialog->grab().save(path);
            QApplication::quit();
        });
    }

    if (app.arguments().contains(QStringLiteral("--screenshot"))) {
        const int index = app.arguments().indexOf(QStringLiteral("--screenshot"));
        const QString path = index + 1 < app.arguments().size() ? app.arguments().at(index + 1) : QStringLiteral("window.png");
        // A page that fetches something needs a moment before it is worth looking at.
        const int delay = qEnvironmentVariableIntValue("KOMOREBI_SHOT_DELAY");
        QTimer::singleShot(delay > 0 ? delay : 700, &app, [&window, path] {
            window.grab().save(path);
            QApplication::quit();
        });
    }

    return app.exec();
}
