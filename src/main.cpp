// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The launcher, from zero. This file does three things: find the data, load the theme, show the
 *  window. Everything else lives in core (files), kit (controls) and ui (the window).
 */

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>

#include "core/Instance.h"
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
    if (instancesRoot.isEmpty()) {
        instancesRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("instances"));
        const QString legacy = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                                   .filePath(QStringLiteral("KomorebiLauncher/instances"));
        if (!QDir(instancesRoot).exists() && QDir(legacy).exists())
            instancesRoot = legacy;
    }

    // The rendering check runs without a window: it renders each control and surface and looks at
    // the pixels, so a black or transparent surface cannot reach a screenshot unnoticed.
    // The rendering check runs without a window and writes its own report; see dev/SelfTest.cpp.
    if (app.arguments().contains(QStringLiteral("--selftest")))
        return Komorebi::Dev::runSelfTest() == 0 ? 0 : 1;
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
