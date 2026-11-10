// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/ToolsPage.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <QDateTime>

#include <thread>

#include "core/Paths.h"
#include "core/Settings.h"
#include "core/util/Cleanup.h"
#include "kit/Kit.h"
#include "services/net/Fetcher.h"

using namespace Komorebi;

namespace {

/// How much a directory tree weighs, in bytes.
qint64 sizeOf(const QString& path)
{
    qint64 total = 0;
    const QDir directory(path);
    if (!directory.exists())
        return 0;
    for (const auto& entry : directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        total += entry.isDir() ? sizeOf(entry.absoluteFilePath()) : entry.size();
    }
    return total;
}

QString humanSize(qint64 bytes)
{
    if (bytes > 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2));
    if (bytes > 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
    return QStringLiteral("%1 KB").arg(bytes / 1024);
}

}  // namespace

ToolsPage::ToolsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Tools"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* hint = new QLabel(tr("Small jobs that do not belong anywhere else."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // The jobs sit on a paper plate, one grid row each: text on the left, the button in one column on
    // the right, so the buttons line up whatever the language does to their width. The plate is wide
    // enough for the longest sentence a translation has: at 680 Japanese wrapped in the middle of a
    // word.
    auto* plate = new QFrame(this);
    plate->setObjectName(QStringLiteral("plate"));
    plate->setStyleSheet(QStringLiteral("QFrame#plate { background: rgba(250, 247, 240, 0.66);"
                                        " border: 1px solid rgba(201, 194, 180, 0.9); border-radius: 6px; }"));
    plate->setMaximumWidth(760);
    auto* grid = new QGridLayout(plate);
    grid->setContentsMargins(18, 14, 18, 14);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(0, 1);
    int rowIndex = 0;

    const auto addRow = [this, plate, grid, &rowIndex](const QString& label, const QString& buttonText,
                                                       void (ToolsPage::*slot)()) {
        auto* text = new QLabel(label, plate);
        text->setWordWrap(true);
        grid->addWidget(text, rowIndex, 0);
        auto* button = new Button(buttonText, plate);
        button->setMinimumWidth(130);
        grid->addWidget(button, rowIndex, 1, Qt::AlignRight | Qt::AlignVCenter);
        connect(button, &QPushButton::clicked, this, slot);
        ++rowIndex;
        return button;
    };

    addRow(tr("Game logs, crash reports and temporary files left behind."), tr("Clean up"), &ToolsPage::cleanJunk);
    addRow(tr("A shortcut on the desktop that opens this launcher."), tr("Create shortcut"), &ToolsPage::createShortcut);
    addRow(tr("The launcher's settings, to carry to another machine."), tr("Export settings"), &ToolsPage::exportSettings);
    addRow(tr("Read settings from a file."), tr("Import settings"), &ToolsPage::importSettings);
    m_downloadButton = addRow(tr("Download one file by its address."), tr("Download a file"), &ToolsPage::downloadFile);

    // The plate fills the page up to its maximum instead of shrinking to its hint: a row whose text
    // is longer than the hint (Japanese) would otherwise wrap inside the widest label.
    layout->addWidget(plate);
    layout->addStretch(1);
}

void ToolsPage::cleanJunk()
{
    // Deleting is not something the player can take back, so it asks first and says what it means.
    if (QMessageBox::question(this, tr("Clean up"),
                              tr("Remove the game logs, crash reports and temporary files? This cannot be undone."))
        != QMessageBox::Yes)
        return;

    const qint64 freed = Komorebi::cleanJunk(dataRoot());
    emit status(freed > 0 ? tr("Cleaned up %1 of logs, crash reports and temporary files").arg(humanSize(freed))
                          : tr("Nothing to clean up"));
}

void ToolsPage::createShortcut()
{
#if defined(Q_OS_WIN)
    // A .url pointing at the executable is the one shortcut Windows makes without a helper library.
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString path = QDir(desktop).filePath(QStringLiteral("Komorebi Lite.lnk"));
    const QString target = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("komorebi-lite.exe"));

    const QString script = QStringLiteral(
        "$shell = New-Object -ComObject WScript.Shell; "
        "$shortcut = $shell.CreateShortcut('%1'); "
        "$shortcut.TargetPath = '%2'; "
        "$shortcut.WorkingDirectory = '%3'; "
        "$shortcut.Save()")
                               .arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(target),
                                    QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));

    QProcess process;
    process.start(QStringLiteral("powershell.exe"), { QStringLiteral("-NoProfile"), QStringLiteral("-Command"), script });
    const bool ok = process.waitForFinished(20000) && process.exitCode() == 0;
    emit status(ok ? tr("Shortcut created on the desktop") : tr("Could not create the shortcut"));
#else
    emit status(tr("Shortcuts are a Windows thing"));
#endif
}

void ToolsPage::exportSettings()
{
    const QString target = QFileDialog::getSaveFileName(this, tr("Export settings"),
                                                        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                                            .filePath(QStringLiteral("komorebi-lite-settings.json")),
                                                        tr("Settings file (*.json)"));
    if (target.isEmpty())
        return;

    QFile source(QDir(dataRoot()).filePath(QStringLiteral("settings.json")));
    if (!source.open(QIODevice::ReadOnly)) {
        emit status(tr("There are no settings to export yet"));
        return;
    }
    const QByteArray data = source.readAll();
    source.close();

    // QSaveFile writes a temporary file beside the target and renames it on commit: a half-written
    // export can never replace a settings file that was already there.
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) {
        emit status(tr("Could not export the settings: %1").arg(out.errorString()));
        return;
    }
    if (out.write(data) != data.size() || !out.commit()) {
        emit status(tr("Could not export the settings: %1").arg(out.errorString()));
        return;
    }
    emit status(tr("Settings exported"));
}

void ToolsPage::importSettings()
{
    const QString source = QFileDialog::getOpenFileName(this, tr("Import settings"), {},
                                                        tr("Settings file (*.json)"));
    if (source.isEmpty())
        return;

    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        emit status(tr("Could not read the settings file: %1").arg(file.errorString()));
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    // Only a JSON object is a settings file. Checking before writing anything means a wrong file
    // picked in the dialog costs nothing instead of leaving the launcher with no settings.
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        emit status(tr("That file is not a settings file"));
        return;
    }

    const QByteArray text = document.toJson(QJsonDocument::Indented);
    const QString target = QDir(dataRoot()).filePath(QStringLiteral("settings.json"));
    QDir().mkpath(QFileInfo(target).absolutePath());

    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) {
        emit status(tr("Could not import the settings: %1").arg(out.errorString()));
        return;
    }
    if (out.write(text) != text.size() || !out.commit()) {
        emit status(tr("Could not import the settings: %1").arg(out.errorString()));
        return;
    }
    emit status(tr("Settings imported; they apply the next time the launcher starts"));
}

void ToolsPage::downloadFile()
{
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Download a file"), tr("Address"), QLineEdit::Normal, {}, &ok);
    if (!ok || url.isEmpty())
        return;

    const QString target = QFileDialog::getSaveFileName(this, tr("Where to save it"),
                                                        QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                                                            .filePath(QUrl(url).fileName()));
    if (target.isEmpty())
        return;

    // A server that never answers holds the request until the timeout: on the window's own thread
    // that froze the launcher for the whole wait, so the fetch runs on its own thread and reports
    // back through the event loop.
    if (m_downloadButton)
        m_downloadButton->setEnabled(false);
    emit status(tr("Downloading…"));

    QPointer<ToolsPage> guard(this);
    std::thread([guard, url, target] {
        const FetchResult result = fetchFile(url, target, {});
        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, target, result] {
                if (guard)
                    guard->finishDownload(target, result.ok, result.error);
            },
            Qt::QueuedConnection);
    }).detach();
}

void ToolsPage::finishDownload(const QString& target, bool ok, const QString& error)
{
    if (m_downloadButton)
        m_downloadButton->setEnabled(true);
    emit status(ok ? tr("Saved to %1").arg(QDir::toNativeSeparators(target))
                   : tr("Download failed: %1").arg(error));
}

void ToolsPage::reload()
{
}
