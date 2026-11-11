// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/LaunchPage.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <mutex>
#include <thread>

#include "core/InstanceConfig.h"
#include "core/Paths.h"
#include "core/auth/AccountStore.h"
#include "core/install/ManualMods.h"
#include "core/install/VersionInstaller.h"
#include "core/launch/LaunchService.h"
#include "core/launch/WindowsTweaks.h"
#include "core/version/VersionResolver.h"
#include "kit/Kit.h"
#include "services/net/Fetcher.h"
#include "ui/InstallDialog.h"
#include "ui/InstanceSettingsDialog.h"

using namespace Komorebi;

/// What a repair is doing, shared with the thread that does it.
struct LaunchPage::RepairState {
    std::atomic<bool> done{ false };
    std::atomic<bool> ok{ false };
    std::atomic<bool> cancelled{ false };
    std::atomic<int> stepsDone{ 0 };
    std::atomic<int> stepsTotal{ 0 };
    std::mutex mutex;
    QString stage;
    QString error;
    int downloaded = 0;
    int skipped = 0;
};

namespace {

QString sizes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 0));
}

/// A log file the launcher can keep for the player, in one place.
QString logPathFor(const QString& versionId)
{
    const QString directory = QDir(dataRoot()).filePath(QStringLiteral("logs"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("%1-%2.log")
                                        .arg(versionId, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
}

}  // namespace

LaunchPage::~LaunchPage()
{
    // Closing the window in the middle of a repair used to let the repair thread write into a state
    // this page had already deleted. The thread keeps its own reference now, so the state outlives
    // the page; the cancellation flag has to stop aiming at it, and the repair is asked to stop.
    if (m_repair) {
        m_repair->cancelled = true;
        setCancellationFlag(nullptr);
        m_repair.reset();
    }
}

LaunchPage::LaunchPage(const Settings& settings, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_store(versionsRoot())
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { padding: 9px 12px; margin: 2px 0px; border-radius: 4px; color: #242e4c; outline: none; }"
        "QListWidget::item:hover { background: rgba(250, 247, 240, 0.55); }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); color: #242e4c; }"));
    m_list->setMinimumWidth(260);
    m_list->setMaximumWidth(320);
    // Long version names used to add a horizontal scrollbar under a list that is only a column wide;
    // eliding the name keeps the row one line and the scrollbar out of the way.
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setWordWrap(false);
    layout->addWidget(m_list, 0);

    auto* plate = new QFrame(this);
    plate->setObjectName(QStringLiteral("detail"));
    plate->setStyleSheet(QStringLiteral("QFrame#detail { background: rgba(250, 247, 240, 0.66);"
                                        " border: 1px solid rgba(201, 194, 180, 0.9); border-radius: 6px; }"));
    auto* plateLayout = new QVBoxLayout(plate);
    plateLayout->setContentsMargins(22, 20, 22, 20);
    plateLayout->setSpacing(10);

    m_title = new QLabel(tr("Select a version"), plate);
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    plateLayout->addWidget(m_title);

    m_body = new QLabel(plate);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    plateLayout->addWidget(m_body);

    // Who the game will play as: switching accounts happens on another page, so the answer has to be
    // visible here without opening the details of the version.
    m_account = new QLabel(plate);
    m_account->setStyleSheet(QStringLiteral("color: rgba(36, 46, 76, 0.72);"));
    plateLayout->addWidget(m_account);

    // the things a player does to a version
    // Two rows rather than one: the labels grow with the language (Japanese is the longest) and a
    // single row of seven buttons ran past the plate and cut the last button in half.
    auto* actions = new QGridLayout();
    actions->setHorizontalSpacing(8);
    actions->setVerticalSpacing(8);
    auto* configure = new Button(tr("Settings"), plate);
    auto* rename = new Button(tr("Rename"), plate);
    auto* describe = new Button(tr("Notes"), plate);
    auto* folder = new Button(tr("Folder"), plate);
    auto* complete = new Button(tr("Complete files"), plate);
    auto* script = new Button(tr("Start script"), plate);
    auto* remove = new Button(tr("Delete"), plate);
    actions->addWidget(configure, 0, 0);
    actions->addWidget(rename, 0, 1);
    actions->addWidget(describe, 0, 2);
    actions->addWidget(folder, 0, 3);
    actions->addWidget(complete, 1, 0);
    actions->addWidget(script, 1, 1);
    actions->addWidget(remove, 1, 2);
    actions->setColumnStretch(4, 1);
    plateLayout->addLayout(actions);
    plateLayout->addStretch(1);

    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(tr("Ready"), plate);
    bottom->addWidget(m_status, 1);
    m_play = new Button(tr("Play"), plate);
    m_play->setPrimary(true);
    m_play->setEnabled(false);
    bottom->addWidget(m_play);
    plateLayout->addLayout(bottom);

    layout->addWidget(plate, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, &LaunchPage::selectRow);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { play(); });
    connect(m_play, &QPushButton::clicked, this, &LaunchPage::play);
    connect(configure, &QPushButton::clicked, this, &LaunchPage::editInstance);
    connect(rename, &QPushButton::clicked, this, &LaunchPage::renameVersion);
    connect(describe, &QPushButton::clicked, this, &LaunchPage::describeVersion);
    connect(folder, &QPushButton::clicked, this, &LaunchPage::openFolder);
    connect(complete, &QPushButton::clicked, this, &LaunchPage::completeFiles);
    connect(script, &QPushButton::clicked, this, &LaunchPage::exportScript);
    connect(remove, &QPushButton::clicked, this, &LaunchPage::removeVersion);
    connect(this, &LaunchPage::status, m_status, &QLabel::setText);

    reload();
}

void LaunchPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshAccount();
}

/// The line above the version: the account the game will play as, or the warning that there is none.
void LaunchPage::refreshAccount()
{
    const QList<MinecraftAccount> accounts = AccountStore::load();
    if (accounts.isEmpty()) {
        m_account->setText(tr("No account signed in: the game starts offline."));
        return;
    }

    const QString active = AccountStore::activeUuid();
    QString name = accounts.first().name;
    for (const auto& account : accounts) {
        if (account.uuid == active)
            name = account.name;
    }
    m_account->setText(tr("Playing as %1").arg(name));
}

void LaunchPage::applySettings(const Settings& settings)
{
    m_settings = settings;
}

void LaunchPage::reload()
{
    m_versions = m_store.scan();
    refreshAccount();

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const auto& version : m_versions) {
        const InstanceConfig config = InstanceConfig::load(version.path);
        const QString title = config.displayName.isEmpty() ? version.id : config.displayName;
        auto* item = new QListWidgetItem(title);
        item->setData(Qt::UserRole, version.id);
        if (!config.displayName.isEmpty())
            item->setToolTip(version.id);
        m_list->addItem(item);
    }

    if (m_versions.isEmpty()) {
        m_selected.clear();
        m_title->setText(tr("No versions installed yet"));
        m_body->setText(tr("Use Install a version in the top right corner to download one."));
        m_play->setEnabled(false);
        emit status(tr("Nothing installed"));
        return;
    }

    int row = 0;
    for (int i = 0; i < m_versions.size(); ++i) {
        if (m_versions.at(i).id == m_selected)
            row = i;
    }
    m_list->setCurrentRow(row);
    // the blocker above swallowed the signal that would have done this
    selectRow(row);
}

void LaunchPage::selectVersion(const QString& versionId)
{
    m_selected = versionId;
    reload();
}

void LaunchPage::selectRow(int row)
{
    if (row < 0 || row >= m_versions.size()) {
        m_selected.clear();
        m_play->setEnabled(false);
        return;
    }

    const auto& version = m_versions.at(row);
    m_selected = version.id;
    m_play->setEnabled(version.hasGameJar);

    const InstanceConfig config = InstanceConfig::load(version.path);
    m_title->setText(config.displayName.isEmpty() ? version.id : config.displayName);

    QString body = tr("Java %1 · %2\nInstalled at: %3\nGame files: %4")
                       .arg(version.javaMajor > 0 ? QString::number(version.javaMajor) : tr("unknown"), sizes(version.size),
                            QDir::toNativeSeparators(version.path), version.hasGameJar ? tr("complete") : tr("missing"));
    if (config.memoryMb > 0)
        body += tr("\nMemory: %1 MB (this version)").arg(config.memoryMb);
    if (!config.jvmArguments.isEmpty())
        body += tr("\nJVM arguments: %1").arg(config.jvmArguments);
    if (!config.gameArguments.isEmpty())
        body += tr("\nGame arguments: %1").arg(config.gameArguments);
    if (config.launchCount > 0)
        body += tr("\nStarted %1 time(s), last %2").arg(config.launchCount).arg(config.lastLaunched);
    // A pack installed short of a file starts and then behaves strangely; the count belongs where the
    // player looks before pressing play, not only in a dialog they may have closed.
    const int owed = readManualMods(version.path).size();
    if (owed > 0)
        body += tr("\n\n%1 file(s) of this pack still have to be fetched by hand. The Content page lists them.")
                    .arg(owed);
    if (!config.description.isEmpty())
        body += QStringLiteral("\n\n") + config.description;

    m_body->setText(body);
    emit status(version.hasGameJar ? tr("Ready") : tr("The game files of this version are missing; complete them"));
}

void LaunchPage::play()
{
    if (m_selected.isEmpty())
        return;
    if (m_game && m_game->state() != QProcess::NotRunning) {
        emit status(tr("The game is already running"));
        return;
    }

    // A launch that fails has to leave something to read afterwards: the status line is one sentence
    // and it is gone as soon as the next thing happens.
    const auto record = [](const QString& version, const QString& text) {
        const QDir logs(QDir(dataRoot()).filePath(QStringLiteral("logs")));
        QDir().mkpath(logs.path());
        QFile file(logs.filePath(QStringLiteral("launcher.log")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
            return;
        file.write(QStringLiteral("[%1] %2: %3\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate), version, text)
                       .toUtf8());
    };

    const PreparedLaunch prepared = prepareLaunch(m_settings, m_selected);
    if (!prepared.ok()) {
        QString reason = prepared.error;
        if (reason.isEmpty())
            reason = tr("%1 file(s) are missing").arg(prepared.plan.missingFiles.size());
        if (!prepared.plan.missingFiles.isEmpty())
            reason += QStringLiteral("\n") + prepared.plan.missingFiles.mid(0, 5).join(QLatin1Char('\n'));
        record(m_selected, QStringLiteral("cannot launch: ") + reason);
        emit status(reason);
        QMessageBox::warning(this, tr("Cannot launch %1").arg(m_selected), reason);
        return;
    }

    record(m_selected, QStringLiteral("launching: ") + prepared.plan.commandLine());

    if (!m_game) {
        m_game = new QProcess(this);
        m_game->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_game, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
            emit status(code == 0 ? tr("Game exited") : tr("The game exited with code %1").arg(code));
            m_play->setEnabled(true);
        });
        connect(m_game, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                emit status(tr("Could not start: Java did not run"));
                m_play->setEnabled(true);
            }
        });
    }

    // the game's own output goes to a log the player can find later
    const QString logPath = logPathFor(m_selected);
    m_game->setStandardOutputFile(logPath, QIODevice::Append);
    m_game->setStandardErrorFile(logPath, QIODevice::Append);
    m_game->setWorkingDirectory(prepared.plan.workingDirectory);
    m_game->start(prepared.plan.program, prepared.plan.arguments);
    if (!m_game->waitForStarted(10000)) {
        record(m_selected, QStringLiteral("java did not start: ") + m_game->errorString());
        emit status(tr("Could not start: Java did not run"));
        QMessageBox::warning(this, tr("Cannot launch %1").arg(m_selected),
                             tr("Java did not start: %1").arg(m_game->errorString()));
        return;
    }

    // count it: the page shows how often a version was started
    InstanceConfig config = InstanceConfig::load(versionDirectory(m_selected));
    ++config.launchCount;
    config.lastLaunched = QDate::currentDate().toString(Qt::ISODate);
    config.save(versionDirectory(m_selected));

    setProcessPriority(m_game->processId(), prepared.plan.priority);

    m_play->setEnabled(false);
    emit status(tr("Game started"));
}

void LaunchPage::install()
{
    InstallDialog dialog(m_store, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.installedId().isEmpty()) {
        m_selected = dialog.installedId();
        reload();
    }
}

void LaunchPage::openFolder()
{
    if (m_selected.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(versionDirectory(m_selected)));
}

void LaunchPage::completeFiles()
{
    if (m_selected.isEmpty() || m_repair)
        return;
    const PreparedLaunch prepared = prepareLaunch(m_settings, m_selected);
    const QStringList missing = prepared.plan.missingFiles;
    if (missing.isEmpty()) {
        emit status(tr("Nothing is missing"));
        return;
    }

    // "Complete files" used to open the installer, which asked the player to pick a version again --
    // and installing that version never touched the one that was actually missing files. The repair
    // below follows inheritsFrom and fetches exactly what the launch plan could not find.
    m_repair = std::make_shared<RepairState>();
    const std::shared_ptr<RepairState> state = m_repair;

    auto* dialog = new QProgressDialog(tr("Completing %1…").arg(m_selected), tr("Cancel"), 0, 0, this);
    dialog->setWindowTitle(tr("Complete files"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setValue(0);
    m_repairDialog = dialog;
    connect(dialog, &QProgressDialog::canceled, this, [state] {
        // The download queue watches this flag and drops what is in flight.
        state->cancelled = true;
    });

    emit status(tr("%1 file(s) missing, filling them in").arg(missing.size()));

    const QString dataRootPath = dataRoot();
    const QString versionId = m_selected;
    setCancellationFlag(&state->cancelled);
    std::thread([state, dataRootPath, versionId] {
        VersionInstaller installer(dataRootPath);
        installer.onProgress = [state](const InstallProgress& progress) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->stage = progress.stage;
            state->stepsDone = progress.done;
            state->stepsTotal = progress.total;
        };
        const InstallResult result = installer.repair(versionId);
        std::lock_guard<std::mutex> lock(state->mutex);
        state->ok = result.ok;
        state->error = result.failed.isEmpty() ? result.error : result.failed.first();
        state->downloaded = result.downloaded;
        state->skipped = result.skipped;
        state->done = true;
    }).detach();

    if (!m_repairPoll) {
        m_repairPoll = new QTimer(this);
        connect(m_repairPoll, &QTimer::timeout, this, &LaunchPage::pollRepair);
    }
    m_repairPoll->start(150);
}

void LaunchPage::pollRepair()
{
    if (!m_repair)
        return;

    {
        std::lock_guard<std::mutex> lock(m_repair->mutex);
        if (m_repairDialog) {
            const int total = m_repair->stepsTotal.load();
            const int done = m_repair->stepsDone.load();
            if (total > 0) {
                m_repairDialog->setRange(0, total);
                m_repairDialog->setValue(done);
            } else {
                m_repairDialog->setRange(0, 0);
            }
            m_repairDialog->setLabelText(m_repair->stage.isEmpty()
                                             ? tr("Completing %1…").arg(m_selected)
                                             : tr("%1 · %2").arg(m_repair->stage, m_selected));
        }
    }

    if (!m_repair->done)
        return;

    // The flag points at this state; it has to be dropped before the state goes away.
    setCancellationFlag(nullptr);
    if (m_repairPoll)
        m_repairPoll->stop();

    // Read under the lock the repair thread writes with, then let the state go: the thread holds the
    // other reference, so the state stays alive until that thread has finished with it.
    bool ok = false;
    bool cancelled = false;
    QString error;
    int downloaded = 0;
    int skipped = 0;
    {
        std::lock_guard<std::mutex> lock(m_repair->mutex);
        ok = m_repair->ok.load();
        cancelled = m_repair->cancelled.load();
        error = m_repair->error;
        downloaded = m_repair->downloaded;
        skipped = m_repair->skipped;
    }

    if (m_repairDialog) {
        m_repairDialog->close();
        m_repairDialog->deleteLater();
        m_repairDialog = nullptr;
    }
    m_repair.reset();

    if (ok) {
        const QString message = downloaded > 0 ? tr("%1 file(s) fetched").arg(downloaded)
                                              : tr("Everything was already there (%1 checked)").arg(skipped);
        emit status(message);
        reload();
        return;
    }

    if (cancelled) {
        emit status(tr("Completing the files was cancelled"));
        return;
    }

    emit status(tr("Could not complete the files: %1").arg(error));
    QMessageBox::warning(this, tr("Complete files"), tr("Could not complete the files: %1").arg(error));
}

void LaunchPage::exportScript()
{
    if (m_selected.isEmpty())
        return;
    const PreparedLaunch prepared = prepareLaunch(m_settings, m_selected);
    if (!prepared.plan.valid()) {
        emit status(prepared.plan.error);
        return;
    }

    const QString path = QDir(versionDirectory(m_selected)).filePath(QStringLiteral("start.bat"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit status(tr("Could not write %1").arg(path));
        return;
    }
    file.write("@echo off\r\ncd /d \"" + prepared.plan.workingDirectory.toUtf8() + "\"\r\n");
    file.write(prepared.plan.commandLine().toUtf8() + "\r\n");
    file.close();
    emit status(tr("Wrote %1").arg(QDir::toNativeSeparators(path)));
}

void LaunchPage::editInstance()
{
    if (m_selected.isEmpty())
        return;

    const QString directory = versionDirectory(m_selected);
    InstanceSettingsDialog dialog(m_selected, InstanceConfig::load(directory), m_settings.memoryMb, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    dialog.result().save(directory);
    reload();
    emit status(tr("Saved the settings of %1").arg(m_selected));
}

void LaunchPage::renameVersion()
{
    if (m_selected.isEmpty())
        return;
    const QString directory = versionDirectory(m_selected);
    InstanceConfig config = InstanceConfig::load(directory);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name (empty: the version id)"),
                                               QLineEdit::Normal, config.displayName, &ok);
    if (!ok)
        return;
    config.displayName = name.trimmed();
    config.save(directory);
    reload();
}

void LaunchPage::describeVersion()
{
    if (m_selected.isEmpty())
        return;
    const QString directory = versionDirectory(m_selected);
    InstanceConfig config = InstanceConfig::load(directory);
    bool ok = false;
    const QString notes = QInputDialog::getMultiLineText(this, tr("Notes"), tr("Notes for this version"),
                                                         config.description, &ok);
    if (!ok)
        return;
    config.description = notes;
    config.save(directory);
    selectRow(m_list->currentRow());
}

void LaunchPage::removeVersion()
{
    if (m_selected.isEmpty())
        return;

    // A loader or a pack is a description on top of a version: deleting the base silently leaves
    // every one of them with "the game files are missing" and no hint about why.
    QStringList dependents;
    const QString versionsRoot = QDir(dataRoot()).filePath(QStringLiteral("versions"));
    for (const auto& version : m_versions) {
        if (version.id == m_selected)
            continue;
        const ResolvedVersion resolved = resolveVersion(versionsRoot, version.id);
        if (resolved.ok() && resolved.chain.contains(m_selected))
            dependents << version.id;
    }

    QString question = tr("Delete %1 and everything in its folder?").arg(m_selected);
    if (!dependents.isEmpty()) {
        question += QStringLiteral("\n\n")
                    + tr("%1 other version(s) are built on it and will stop working: %2")
                          .arg(dependents.size())
                          .arg(dependents.mid(0, 5).join(QStringLiteral(", ")));
    }
    if (QMessageBox::question(this, tr("Delete"), question) != QMessageBox::Yes)
        return;

    QDir(versionDirectory(m_selected)).removeRecursively();
    m_selected.clear();
    reload();
}
