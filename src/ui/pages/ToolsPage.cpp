// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/ToolsPage.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <QDateTime>

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

    // The jobs sit on a paper plate, a row each: text on the left, the button right after it.
    auto* plate = new QFrame(this);
    plate->setObjectName(QStringLiteral("plate"));
    plate->setStyleSheet(QStringLiteral("QFrame#plate { background: rgba(250, 247, 240, 0.66);"
                                        " border: 1px solid rgba(201, 194, 180, 0.9); border-radius: 6px; }"));
    plate->setMaximumWidth(680);
    auto* grid = new QVBoxLayout(plate);
    grid->setContentsMargins(18, 14, 18, 14);
    grid->setSpacing(10);

    const auto addRow = [this, plate, grid](const QString& label, const QString& buttonText, void (ToolsPage::*slot)()) {
        auto* row = new QHBoxLayout();
        row->setSpacing(12);
        auto* text = new QLabel(label, plate);
        text->setWordWrap(true);
        text->setMinimumWidth(320);
        row->addWidget(text, 1);
        auto* button = new Button(buttonText, plate);
        button->setMinimumWidth(130);
        row->addWidget(button, 0);
        connect(button, &QPushButton::clicked, this, slot);
        grid->addLayout(row);
    };

    addRow(tr("Game logs, crash reports and temporary files left behind."), tr("Clean up"), &ToolsPage::cleanJunk);
    addRow(tr("A shortcut on the desktop that opens this launcher."), tr("Create shortcut"), &ToolsPage::createShortcut);
    addRow(tr("The launcher's settings, to carry to another machine."), tr("Export settings"), &ToolsPage::exportSettings);
    addRow(tr("Read settings from a file."), tr("Import settings"), &ToolsPage::importSettings);
    addRow(tr("Download one file by its address."), tr("Download a file"), &ToolsPage::downloadFile);

    layout->addWidget(plate, 0, Qt::AlignLeft);
    layout->addStretch(1);
}

void ToolsPage::cleanJunk()
{
    const qint64 freed = Komorebi::cleanJunk(dataRoot());
    emit status(tr("Cleaned up %1").arg(humanSize(freed)));
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

    const QString source = QDir(dataRoot()).filePath(QStringLiteral("settings.json"));
    if (QFile::exists(target))
        QFile::remove(target);
    emit status(QFile::copy(source, target) ? tr("Settings exported") : tr("Could not export the settings"));
}

void ToolsPage::importSettings()
{
    const QString source = QFileDialog::getOpenFileName(this, tr("Import settings"), {},
                                                        tr("Settings file (*.json)"));
    if (source.isEmpty())
        return;

    const QString target = QDir(dataRoot()).filePath(QStringLiteral("settings.json"));
    if (QFile::exists(target))
        QFile::remove(target);
    if (!QFile::copy(source, target)) {
        emit status(tr("Could not import the settings"));
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

    const FetchResult result = fetchFile(url, target, {});
    emit status(result.ok ? tr("Saved to %1").arg(QDir::toNativeSeparators(target))
                          : tr("Download failed: %1").arg(result.error));
}

void ToolsPage::reload()
{
}
