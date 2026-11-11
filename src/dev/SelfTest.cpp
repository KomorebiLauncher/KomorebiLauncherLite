// SPDX-License-Identifier: GPL-3.0-only
#include "dev/SelfTest.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>

#include "core/auth/MicrosoftAuth.h"
#include "core/Paths.h"
#include "core/install/ManualMods.h"
#include "kit/Kit.h"
#include "ui/ManualModsDialog.h"
#include "ui/PackVersionDialog.h"
#include "ui/pages/SimplePages.h"

namespace Komorebi::Dev {

namespace {

int g_failures = 0;
QStringList g_report;

int luminance(const QColor& colour)
{
    return (colour.red() * 299 + colour.green() * 587 + colour.blue() * 114) / 1000;
}

void report(bool ok, const QString& what, const QString& detail)
{
    if (!ok)
        ++g_failures;
    const QString line = QStringLiteral("%1  %2%3").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"),
                                                       what, detail.isEmpty() ? QString() : QStringLiteral(" — ") + detail);
    g_report << line;
}

/// Renders a widget off-screen and hands back its pixels.
QImage render(QWidget* widget, QSize size)
{
    widget->resize(size);
    widget->move(-4000, -4000);
    widget->show();
    QApplication::processEvents();
    const QImage image = widget->grab().toImage();
    image.save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("selftest-%1.png").arg(QString::fromLatin1(widget->metaObject()->className()))));
    widget->hide();
    return image;
}

}  // namespace

int runSelfTest()
{
    g_failures = 0;
    const QPalette palette = QApplication::palette();
    const QColor paper = palette.color(QPalette::Window);
    const QColor ink = palette.color(QPalette::WindowText);
    const QColor plum = palette.color(QPalette::Link);

    g_report << QStringLiteral("rendering self-test");

    // 1. the palette itself: a launcher whose window colour is dark is already broken
    report(luminance(paper) > 200, "window colour is paper", paper.name());
    report(luminance(ink) < 120, "text colour is ink", ink.name());

    // 2. a plain window: whatever paints the surface must not leave black behind
    {
        QWidget surface;
        const QImage image = render(&surface, QSize(200, 120));
        report(luminance(image.pixelColor(100, 60)) > 200, "empty surface is paper",
               image.pixelColor(100, 60).name());
    }

    // 3. the kit's buttons: the secondary one is paper, the primary one is the accent
    {
        Komorebi::Button secondary(QStringLiteral("cancel"));
        const QImage image = render(&secondary, QSize(140, 34));
        report(luminance(image.pixelColor(12, 17)) > 180, "button surface is paper",
               image.pixelColor(12, 17).name());

        Komorebi::Button primary(QStringLiteral("launch"));
        primary.setPrimary(true);
        const QImage imagePrimary = render(&primary, QSize(140, 34));
        report(imagePrimary.pixelColor(12, 17) == plum, "primary button is plum",
               imagePrimary.pixelColor(12, 17).name());
    }

    // 4. the field: paper with a visible edge, and the text is the ink colour
    {
        Komorebi::Field field;
        field.setText(QStringLiteral("1.20.1"));
        const QImage image = render(&field, QSize(200, 34));
        const QColor inside = image.pixelColor(100, 6);
        report(luminance(inside) > 180, "field surface is paper", inside.name());
        // The edge is what makes an input look like an input: the style used to fill over the plate
        // with the same paper the window is painted with, so a check that only ruled out black passed
        // while every field in the launcher had no frame at all.
        const QColor edge = image.pixelColor(0, 17);
        report(edge != inside, "field edge is drawn", edge.name());
    }

    // 5. the combo: its own label is painted, so the middle of the row is not empty paper
    {
        Komorebi::Combo combo;
        combo.addItems({ QStringLiteral("sidebar"), QStringLiteral("rail") });
        const QImage image = render(&combo, QSize(220, 34));
        bool foundInk = false;
        for (int x = 12; x < 120 && !foundInk; ++x)
            for (int y = 8; y < 26 && !foundInk; ++y)
                if (image.pixelColor(x, y) == ink)
                    foundInk = true;
        report(foundInk, "combo paints its label", QString());
    }

    // 6. a dialog: the surface the old code kept getting wrong
    {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("…"), &dialog));
        const QImage image = render(&dialog, QSize(240, 140));
        report(luminance(image.pixelColor(120, 100)) > 180, "dialog surface is paper",
               image.pixelColor(120, 100).name());
    }

    // 7. a popup menu: a top-level window of its own, so it does not inherit the above
    {
        QMenu menu;
        menu.addAction(QStringLiteral("rename"));
        const QImage image = render(&menu, QSize(160, 80));
        report(luminance(image.pixelColor(80, 40)) > 180, "menu surface is paper",
               image.pixelColor(80, 40).name());
    }

    // 8. the application id signing in goes through: it is compiled in, so it cannot be missing
    {
        const QString client = MicrosoftAuth::clientId();
        report(!client.isEmpty(), "an app id is always available", client);
    }

    // 9. the data directory: the folder the launcher sits in, with an override for test runs
    {
        // A run that sets KOMOREBI_DATA (the packaging check, run-local.bat) is pointed somewhere else
        // on purpose, so the default and the environment variable are checked separately.
        const QString ownFolder = QDir::cleanPath(QCoreApplication::applicationDirPath());
        report(defaultDataRoot() == ownFolder, "the default data directory is the launcher's own folder",
               QStringLiteral("%1 vs %2").arg(defaultDataRoot(), ownFolder));

        const QString fromEnvironment = dataRootFromEnvironment()
                                            ? QDir::cleanPath(qEnvironmentVariable("KOMOREBI_DATA"))
                                            : defaultDataRoot();
        report(dataRoot() == fromEnvironment, "the data directory is that folder, or the one KOMOREBI_DATA names",
               QStringLiteral("%1 vs %2").arg(dataRoot(), fromEnvironment));

        QTemporaryDir scratch;
        if (!scratch.isValid()) {
            report(false, "temporary directory for the data directory test", QString());
        } else {
            const QString previous = qEnvironmentVariable("KOMOREBI_DATA");

            qputenv("KOMOREBI_DATA", QDir::toNativeSeparators(scratch.path()).toUtf8());
            report(dataRootFromEnvironment(), "KOMOREBI_DATA is noticed", scratch.path());
            report(dataRoot() == QDir::cleanPath(scratch.path()), "KOMOREBI_DATA picks the data directory",
                   dataRoot());
            report(dataRootIsWritable(nullptr), "a writable folder passes the check", scratch.path());

            // A file where the directory should be: making the directory fails, which is the same
            // answer as a folder that refuses to be written to.
            QTemporaryDir holding;
            const QString notAFolder = QDir(holding.path()).filePath(QStringLiteral("data"));
            QFile blocker(notAFolder);
            if (blocker.open(QIODevice::WriteOnly)) {
                blocker.close();
                qputenv("KOMOREBI_DATA", QDir::toNativeSeparators(notAFolder).toUtf8());
                report(!dataRootIsWritable(nullptr), "a folder that cannot be made is refused", notAFolder);
            } else {
                report(false, "a file to stand where the data directory should be", notAFolder);
            }

            if (previous.isEmpty())
                qunsetenv("KOMOREBI_DATA");
            else
                qputenv("KOMOREBI_DATA", previous.toUtf8());
        }
    }

    // 10. spotting a data directory: a moved folder must not pass for an empty one
    {
        QTemporaryDir empty;
        QTemporaryDir filled;
        if (!empty.isValid() || !filled.isValid()) {
            report(false, "temporary directories for the data directory test", QString());
        } else {
            report(!looksLikeDataRoot(empty.path()), "an empty directory is not a data directory", QString());
            report(!looksLikeDataRoot(empty.filePath(QStringLiteral("gone"))),
                   "a directory that is not there is not a data directory", QString());

            QFile settings(filled.filePath(QStringLiteral("settings.json")));
            if (settings.open(QIODevice::WriteOnly)) {
                settings.write("{}");
                settings.close();
            }
            report(looksLikeDataRoot(filled.path()), "settings.json marks a data directory", QString());
        }
    }

    // 11. the log page: a log written while the launcher is running must appear when the page opens
    {
        QTemporaryDir root;
        if (!root.isValid()) {
            report(false, "temporary directory for the log page test", QString());
        } else {
            const QString previous = qEnvironmentVariable("KOMOREBI_DATA");
            qputenv("KOMOREBI_DATA", QDir::toNativeSeparators(root.path()).toUtf8());

            LogsPage page;
            auto* list = page.findChild<QListWidget*>();
            report(list != nullptr, "the log page has a list of log files", QString());

            // Written after the page was built: starting the game and then opening the page is the
            // order players use, and the page used to show the empty list it had built at startup.
            QDir().mkpath(root.filePath(QStringLiteral("logs")));
            QFile log(root.filePath(QStringLiteral("logs/20261111-120000.log")));
            if (log.open(QIODevice::WriteOnly)) {
                log.write("hello\n");
                log.close();
            }

            if (list) {
                page.show();
                report(list->count() == 1, "a log written after the page was built shows up when it opens",
                       QStringLiteral("%1 item(s)").arg(list->count()));
                // The list is only the way in; the log itself is read into the viewer beside it, and
                // the newest file is opened without anyone clicking.
                auto* viewer = page.findChild<QPlainTextEdit*>();
                report(viewer != nullptr, "the log page has a viewer for the file itself", QString());
                if (viewer)
                    report(viewer->toPlainText().contains(QStringLiteral("hello")),
                           "the newest log is already open in the viewer", viewer->toPlainText().trimmed());
                page.hide();
            }

            if (previous.isEmpty())
                qunsetenv("KOMOREBI_DATA");
            else
                qputenv("KOMOREBI_DATA", previous.toUtf8());
        }
    }

    {
        // A combo box with a list longer than the screen used to open its popup at the top of the
        // screen: Qt sized and placed it for the whole list before the cap was applied. Where it
        // ends up is checked here, since a person only notices it when they click the box.
        QDialog host;
        auto* combo = new Komorebi::Combo(&host);
        auto* hostLayout = new QVBoxLayout(&host);
        hostLayout->addWidget(new QLabel(QStringLiteral("popup placement"), &host));
        hostLayout->addStretch(1);
        hostLayout->addWidget(combo);
        hostLayout->addStretch(1);
        for (int i = 0; i < 300; ++i)
            combo->addItem(QStringLiteral("version %1").arg(i));

        host.show();
        combo->showPopup();
        QWidget* popup = combo->view() ? combo->view()->window() : nullptr;
        report(popup != nullptr && popup != combo, "a long combo box opens a popup", QString());
        if (popup && popup != combo) {
            const QRect rect(popup->mapToGlobal(QPoint(0, 0)), popup->size());
            const QScreen* screen = QGuiApplication::screenAt(rect.center());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            const QRect area = screen ? screen->availableGeometry() : rect;

            report(rect.height() <= 400, "the popup is capped instead of running off the screen",
                   QStringLiteral("%1 px tall").arg(rect.height()));
            report(area.contains(rect), "the popup sits on the screen the combo box is on",
                   QStringLiteral("%1,%2 %3x%4 in %5,%6 %7x%8")
                       .arg(rect.left()).arg(rect.top()).arg(rect.width()).arg(rect.height())
                       .arg(area.left()).arg(area.top()).arg(area.width()).arg(area.height()));
            combo->hidePopup();
        }
        host.hide();
    }
    // 12. the files a pack cannot fetch: the list beside the version, and hokora, where a player puts
    // what they downloaded by hand. The two mistakes worth guarding are taking the newest version of
    // a mod for the one the pack asked for, and refusing a file the browser saved under a new name.
    {
        QTemporaryDir root;
        if (!root.isValid()) {
            report(false, "temporary directory for the hokora test", QString());
        } else {
            const QString previous = qEnvironmentVariable("KOMOREBI_DATA");
            qputenv("KOMOREBI_DATA", QDir::toNativeSeparators(root.path()).toUtf8());

            const QString pack = versionDirectory(QStringLiteral("test-pack-1.0"));
            QDir().mkpath(QDir(pack).filePath(QStringLiteral("mods")));
            QDir().mkpath(hokoraDirectory());

            const QByteArray bytes("a jar the pack asked for");
            ManualMod wanted;
            wanted.name = QStringLiteral("JEI");
            wanted.version = QStringLiteral("jei-1.20.1-15.2.0.27");
            wanted.fileName = QStringLiteral("jei-1.20.1-15.2.0.27.jar");
            wanted.sha1 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
            wanted.pageUrl = QStringLiteral("https://www.curseforge.com/minecraft/mc-mods/jei/files/1");
            wanted.projectId = 238222;
            wanted.fileId = 1;

            const auto writeFile = [](const QString& path, const QByteArray& content) {
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    return false;
                file.write(content);
                return true;
            };

            const QString asked = QDir(hokoraDirectory()).filePath(wanted.fileName);
            report(writeManualMods(pack, { wanted }), "the list of files to fetch is written beside the version",
                   QString());
            report(readManualMods(pack).size() == 1, "the list comes back with its entry", QString());

            // The dialog is rendered too: a table with a page for each file and the line saying where
            // the files go is the whole feature, and selftest-ManualModsDialog.png is what it looks like.
            {
                ManualModsDialog dialog(QStringLiteral("test-pack-1.0"));
                const QImage image = render(&dialog, QSize(680, 420));
                report(!image.isNull() && dialog.remaining() == 1, "the list of files to fetch opens with its entry",
                       QStringLiteral("%1x%2").arg(image.width()).arg(image.height()));
            }

            writeFile(asked, "the newest version of the mod, which is not the one asked for");
            const HokoraHarvest refused = harvestHokora(pack);
            report(refused.taken == 0 && refused.rejected.contains(wanted.fileName),
                   "a file with the wanted name but other contents is refused",
                   refused.rejected.join(QStringLiteral(", ")));
            report(QFileInfo::exists(asked), "the refused file is left where the player put it", asked);
            report(readManualMods(pack).size() == 1, "the refused file is still owed", QString());

            // A browser that saved the download as "jei (1).jar" saved the right file; the pack knows
            // it by its contents, and the version needs it under the name its manifest lists.
            const QString renamed = QDir(hokoraDirectory()).filePath(QStringLiteral("jei (1).jar"));
            report(writeFile(renamed, bytes), "a second file in hokora", renamed);
            const HokoraHarvest taken = harvestHokora(pack);
            report(taken.taken == 1, "the file whose contents match is taken", QStringLiteral("%1 taken").arg(taken.taken));
            report(QFileInfo::exists(QDir(pack).filePath(QStringLiteral("mods/%1").arg(wanted.fileName))),
                   "it lands in the mods folder under the name the pack asked for", QString());
            report(readManualMods(pack).isEmpty(), "nothing is owed any more", QString());
            report(!QFileInfo::exists(QDir(pack).filePath(QStringLiteral("manual-mods.json"))),
                   "the list removes itself when nothing is owed", QString());

            if (previous.isEmpty())
                qunsetenv("KOMOREBI_DATA");
            else
                qputenv("KOMOREBI_DATA", previous.toUtf8());
        }
    }

    // The version picker is rendered against a fetcher that answers from a canned list. What is being
    // tested is the dialog itself -- that it comes up, that it lists the files the source names, and
    // that it starts on the release rather than on the beta filed above it. The real fetchers belong
    // to the sources and talk to the network, which a self-test must not do.
    {
        const auto canned = [](int offset, int limit, int* total, QString* error) {
            Q_UNUSED(limit);
            if (error)
                error->clear();
            if (total)
                *total = 3;
            QList<Komorebi::CurseForgeFile> files;
            if (offset > 0)
                return files;
            const auto make = [](int id, const QString& version, const QString& kind, const QString& date) {
                Komorebi::CurseForgeFile file;
                file.id = id;
                file.fileName = QStringLiteral("all-the-mods-11-%1.jar").arg(version);
                file.displayName = file.fileName;
                file.version = version;
                file.releaseType = kind;
                file.date = date;
                file.size = 120LL * 1024 * 1024;
                file.gameVersions = QStringList{ QStringLiteral("NeoForge"), QStringLiteral("26.1.2") };
                return file;
            };
            files << make(30, QStringLiteral("2.0.0-beta"), QStringLiteral("2"), QStringLiteral("2026-09-03T10:00:00Z"))
                  << make(29, QStringLiteral("1.9.1"), QStringLiteral("1"), QStringLiteral("2026-09-02T10:00:00Z"))
                  << make(28, QStringLiteral("1.9.0"), QStringLiteral("1"), QStringLiteral("2026-09-01T10:00:00Z"));
            return files;
        };

        PackVersionDialog dialog(QStringLiteral("All the Mods 11"), canned);
        QElapsedTimer wait;
        wait.start();
        while (dialog.fileCount() == 0 && wait.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 50);

        const QImage image = render(&dialog, QSize(760, 460));
        report(dialog.fileCount() == 3, "the files of a pack are listed",
               QStringLiteral("%1 files").arg(dialog.fileCount()));
        report(dialog.selected().version == QStringLiteral("1.9.1"),
               "the picker starts on the newest release, not on the beta filed above it", dialog.selected().version);
        report(!image.isNull(), "the list of versions opens",
               QStringLiteral("%1x%2").arg(image.width()).arg(image.height()));
    }

    g_report << (g_failures == 0 ? QStringLiteral("self-test passed") : QStringLiteral("self-test failed: %1").arg(g_failures));

    QFile out(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("selftest.log")));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(g_report.join(QLatin1Char('\n')).toUtf8());
    return g_failures;
}

}  // namespace Komorebi::Dev
