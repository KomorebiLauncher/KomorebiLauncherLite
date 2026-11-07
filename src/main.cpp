// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The launcher, from zero. This file does three things: find the data, load the theme, show the
 *  window. Everything else lives in core (files), kit (controls) and ui (the window).
 */

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>

#include "core/Instance.h"
#include "core/Paths.h"
#include "core/launch/LaunchPlan.h"
#include "services/net/Fetcher.h"
#include "core/version/VersionJson.h"
#include "dev/SelfTest.h"
#include "theme/Theme.h"
#include "ui/AppWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KomorebiNext"));
    QApplication::setOrganizationName(QStringLiteral("KomorebiLauncher"));

    // Themes travel next to the binary while the launcher is young; a packaged build would read
    // them from the data directory instead.
    const QString themeDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("theme/light"));
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
    const int planIndex = arguments.indexOf(QStringLiteral("--launch-plan"));
    if (planIndex >= 0 && planIndex + 1 < arguments.size()) {
        Komorebi::LaunchRequest request;
        request.version = Komorebi::VersionJson::fromFile(arguments.at(planIndex + 1));

        request.librariesRoot = Komorebi::librariesRoot();
        request.assetsRoot = Komorebi::assetsRoot();
        request.nativesRoot = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("natives"));

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
        return plan.valid() ? 0 : 1;
    }

    // Launch for real: fetch what the version needs, build the plan, start the process and collect
    // its output. This is the development path for M2/M3 and prints everything it did.
    const int launchIndex = arguments.indexOf(QStringLiteral("--launch"));
    if (launchIndex >= 0 && launchIndex + 1 < arguments.size()) {
        const QString versionPath = arguments.at(launchIndex + 1);
        Komorebi::VersionJson version = Komorebi::VersionJson::fromFile(versionPath);

        QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launch.log")));
        out.open(QIODevice::WriteOnly | QIODevice::Truncate);
        const auto note = [&out](const QString& line) {
            out.write(line.toUtf8());
            out.write("\n");
            out.flush();
        };

        note(QStringLiteral("version %1, main %2, java %3").arg(version.id, version.mainClass).arg(version.javaMajor));

        // the game jar
        const QString clientJar = QDir(Komorebi::librariesRoot())
                                      .filePath(QStringLiteral("net/minecraft/client/%1/client-%1.jar").arg(version.id));
        if (version.clientJar.valid()) {
            const Komorebi::FetchResult fetched = Komorebi::fetchFile(version.clientJar.url, clientJar, version.clientJar.sha1);
            note(QStringLiteral("client jar target: %1").arg(clientJar));
            note(QStringLiteral("client jar url: %1").arg(version.clientJar.url));
            note(QStringLiteral("client jar: %1%2").arg(fetched.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                                                         fetched.alreadyPresent ? QStringLiteral(" (cached)") : QString()));
            if (!fetched.ok)
                note(QStringLiteral("  %1").arg(fetched.error));
        } else {
            note(QStringLiteral("client jar: the manifest does not say where it is"));
        }

        // the asset index: the game reads it at start, so it has to be there
        if (version.assetIndex.valid()) {
            const QString indexFile = QDir(Komorebi::assetsRoot()).filePath(version.assetIndex.path);
            const Komorebi::FetchResult fetched = Komorebi::fetchFile(version.assetIndex.url, indexFile, version.assetIndex.sha1);
            note(QStringLiteral("asset index: %1%2").arg(fetched.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                                                          fetched.alreadyPresent ? QStringLiteral(" (cached)") : QString()));
        }

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

    AppWindow window(theme, instancesRoot);
    window.show();

    // A development hook: render the window once and quit, so a change can be looked at without a
    // person in the loop. It is not part of the launcher's own behaviour.
    if (app.arguments().contains(QStringLiteral("--screenshot"))) {
        const int index = app.arguments().indexOf(QStringLiteral("--screenshot"));
        const QString path = index + 1 < app.arguments().size() ? app.arguments().at(index + 1) : QStringLiteral("window.png");
        QTimer::singleShot(700, &app, [&window, path] {
            window.grab().save(path);
            QApplication::quit();
        });
    }

    return app.exec();
}
