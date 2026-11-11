// SPDX-License-Identifier: GPL-3.0-only
#include "ui/InstallDialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <mutex>
#include <thread>

#include "core/Paths.h"
#include "core/install/VersionInstaller.h"
#include "core/install/LoaderInstaller.h"
#include "core/install/OptiFineInstaller.h"
#include "core/content/PrismMeta.h"
#include "core/install/ForgeInstaller.h"
#include "kit/Kit.h"
#include "services/net/Fetcher.h"

using namespace Komorebi;

struct InstallDialog::State {
    std::atomic<bool> running{ false };
    std::atomic<bool> finished{ false };
    std::atomic<bool> ok{ false };
    std::atomic<int> done{ 0 };
    std::atomic<int> total{ 0 };
    std::atomic<int> downloaded{ 0 };
    std::atomic<int> skipped{ 0 };
    /// The version list is fetched once, in a thread, and then only filtered by the kind combo.
    std::atomic<bool> loading{ false };
    std::atomic<bool> manifestReady{ false };
    /// Set when the dialog goes away: the install thread watches it through the downloader.
    std::atomic<bool> cancelled{ false };
    std::mutex mutex;
    VersionManifest manifest;
    QString manifestError;
    QString stage;
    QString current;
    QString error;
    QString versionId;
};

InstallDialog::~InstallDialog()
{
    // Closing the dialog used to leave the install running into a state that nothing read any more,
    // and the state itself was never freed. The thread keeps its own reference, so it can finish
    // safely; asking it to stop is what turns "closed while downloading" into a stopped download.
    if (m_state) {
        m_state->cancelled = true;
        setCancellationFlag(nullptr);
    }
}

InstallDialog::InstallDialog(const VersionStore& store, QWidget* parent)
    : QDialog(parent), m_store(store), m_state(std::make_shared<State>())
{
    setWindowTitle(tr("Install a version"));
    setMinimumWidth(520);
    resize(620, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("Select a version"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    m_kind = new Komorebi::Combo(this);
    m_kind->addItem(tr("Releases"));
    m_kind->addItem(tr("Snapshots"));
    m_kind->addItem(tr("All"));
    row->addWidget(m_kind);

    m_versions = new Komorebi::Combo(this);
    row->addWidget(m_versions, 1);
    layout->addLayout(row);

    auto* loaderRow = new QHBoxLayout();
    loaderRow->setSpacing(8);
    loaderRow->addWidget(new QLabel(tr("Loader"), this));
    m_loader = new Komorebi::Combo(this);
    m_loader->addItem(tr("Vanilla (no loader)"), QString());
    m_loader->addItem(tr("Fabric"), QStringLiteral("fabric"));
    m_loader->addItem(tr("Quilt"), QStringLiteral("quilt"));
    m_loader->addItem(tr("Forge (newest)"), QStringLiteral("forge"));
    m_loader->addItem(tr("NeoForge (newest)"), QStringLiteral("neoforge"));
    m_loader->addItem(tr("OptiFine (newest)"), QStringLiteral("optifine"));
    loaderRow->addWidget(m_loader, 1);
    layout->addLayout(loaderRow);

    m_hint = new QLabel(tr("Fetching the version list…"), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_progressLog = new QPlainTextEdit(this);
    m_progressLog->setReadOnly(true);
    m_progressLog->setMaximumBlockCount(200);
    m_progressLog->setPlaceholderText(tr("Install progress shows up here"));
    layout->addWidget(m_progressLog, 1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    m_install = new Komorebi::Button(tr("Install"), this);
    m_install->setPrimary(true);
    m_install->setEnabled(false);
    m_close = new Komorebi::Button(tr("Close"), this);
    buttons->addWidget(m_install);
    buttons->addWidget(m_close);
    layout->addLayout(buttons);

    connect(m_close, &QPushButton::clicked, this, [this] {
        // An install that went through is a result the page needs: it hands the new version back so
        // the list shows it selected. Rejecting here would throw that away and leave the old
        // selection in place.
        if (m_installed.isEmpty())
            reject();
        else
            accept();
    });
    connect(m_install, &QPushButton::clicked, this, &InstallDialog::startInstall);
    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { loadManifest(); });

    m_poll = new QTimer(this);
    m_poll->setInterval(200);

    m_autoClose = new QTimer(this);
    m_autoClose->setSingleShot(true);
    m_autoClose->setInterval(1500);
    connect(m_autoClose, &QTimer::timeout, this, &QDialog::accept);
    connect(m_poll, &QTimer::timeout, this, &InstallDialog::pollProgress);

    QTimer::singleShot(0, this, &InstallDialog::loadManifest);
}

void InstallDialog::loadManifest()
{
    if (m_state->running)
        return;

    // Switching between releases and snapshots only filters what is already here: the list is asked
    // for once per dialog.
    bool cached = false;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        cached = !m_state->manifest.versions.isEmpty();
    }
    if (cached) {
        applyManifest();
        return;
    }
    if (m_state->loading)
        return;

    m_state->loading = true;
    m_hint->setText(tr("Fetching the version list…"));
    m_install->setEnabled(false);

    // The list is a network request, and the dialog is on screen while it runs: fetching it on the
    // UI thread froze the whole window behind a busy cursor until Mojang answered.
    const std::shared_ptr<State> state = m_state;
    std::thread([state] {
        QString error;
        VersionManifest manifest = VersionManifest::fetch(&error);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->manifest = std::move(manifest);
            state->manifestError = error;
        }
        state->manifestReady = true;
    }).detach();

    m_poll->start();
}

void InstallDialog::applyManifest()
{
    VersionManifest manifest;
    QString error;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        manifest = m_state->manifest;
        error = m_state->manifestError;
    }
    m_state->loading = false;
    m_state->manifestReady = false;

    if (manifest.versions.isEmpty()) {
        m_hint->setText(tr("Could not fetch the version list: %1").arg(error));
        if (!m_state->running)
            m_poll->stop();
        return;
    }

    const int kind = m_kind->currentIndex();
    m_versions->clear();
    for (const auto& version : manifest.versions) {
        if (kind == 0 && version.type != QLatin1String("release"))
            continue;
        if (kind == 1 && version.type != QLatin1String("snapshot"))
            continue;
        m_versions->addItem(version.id, version.id);
    }

    m_hint->setText(tr("%1 versions available, newest release %2, newest snapshot %3")
                        .arg(m_versions->count())
                        .arg(manifest.latestRelease, manifest.latestSnapshot));
    m_install->setEnabled(m_versions->count() > 0);
    if (!m_state->running)
        m_poll->stop();
}

void InstallDialog::startInstall()
{
    if (m_state->running)
        return;
    const QString id = m_versions->currentData().toString();
    if (id.isEmpty())
        return;

    m_state->running = true;
    m_state->finished = false;
    // Another install cancels the close that the previous one asked for.
    m_autoClose->stop();
    m_state->done = 0;
    m_state->total = 0;
    m_install->setEnabled(false);
    m_progressLog->clear();
    m_progressLog->appendPlainText(tr("Installing %1…").arg(id));
    m_hint->setText(tr("Installing %1").arg(id));

    const QString dataRoot = Komorebi::dataRoot();
    const QString loader = m_loader->currentData().toString();
    const std::shared_ptr<State> state = m_state;
    // The close button, and the window behind it, can stop this install mid-download.
    setCancellationFlag(&state->cancelled);
    std::thread([state, dataRoot, id, loader] {
        VersionInstaller installer(dataRoot);
        installer.onProgress = [state](const InstallProgress& progress) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->stage = progress.stage;
            state->current = progress.current;
            state->done = progress.done;
            state->total = progress.total;
        };
        const InstallResult result = installer.install(id, true);
        if (!result.ok) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = result.failed.isEmpty() ? result.error : result.failed.first();
            state->downloaded = result.downloaded;
            state->skipped = result.skipped;
            state->finished = true;
            return;
        }

        // A loader builds on the version that was just installed, so it is a second step.
        LoaderResult loaderResult;
        loaderResult.ok = true;
        if (loader == QLatin1String("forge")) {
            const ForgeInstallResult forge = installLoader(LoaderFamily::Forge, dataRoot, id, {}, [state](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->stage = progress.stage;
                state->current = progress.current;
            });
            loaderResult.ok = forge.ok;
            loaderResult.error = forge.error;
            loaderResult.versionId = forge.versionId;
        } else if (loader == QLatin1String("neoforge")) {
            // NeoForge is fully described by its component; the installer its wrapper needs is just
            // one of the files that component lists, so this step only writes them down.
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->stage = QStringLiteral("neoforge");
                state->current.clear();
            }
            QString error;
            QString installed;
            const QString component = PrismMeta::install(QStringLiteral("net.neoforged"), id, {}, &installed, &error);
            loaderResult.ok = !component.isEmpty();
            loaderResult.error = error;
            loaderResult.versionId = component;
        } else if (loader == QLatin1String("optifine")) {
            // OptiFine patches the game that was just installed, so it too is a second step.
            const OptiFineResult optifine = installOptiFine(dataRoot, id, {}, [state](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->stage = progress.stage;
                state->current = progress.current;
            });
            loaderResult.ok = optifine.ok;
            loaderResult.error = optifine.error;
            loaderResult.versionId = optifine.versionId;
        } else if (!loader.isEmpty()) {
            LoaderChoice choice;
            choice.kind = loader;
            loaderResult = installLoader(dataRoot, id, choice, [state](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->stage = progress.stage;
                state->done = progress.done;
                state->total = progress.total;
            });
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = loaderResult.ok ? QString() : loaderResult.error;
            state->downloaded = result.downloaded + loaderResult.downloaded;
            state->skipped = result.skipped + loaderResult.skipped;
            state->versionId = loaderResult.versionId.isEmpty() ? id : loaderResult.versionId;
        }
        state->ok = loaderResult.ok;
        state->finished = true;
    }).detach();

    m_poll->start();
}

void InstallDialog::pollProgress()
{
    if (m_state->manifestReady) {
        applyManifest();
        if (!m_state->running)
            return;
    }

    const int done = m_state->done.load();
    const int total = m_state->total.load();
    QString stage;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        stage = m_state->stage;
    }

    if (total > 0) {
        const int percent = done * 100 / total;
        m_hint->setText(tr("%1 %2% (%3/%4)").arg(stage).arg(percent).arg(done).arg(total));
    } else if (!stage.isEmpty()) {
        m_hint->setText(stage);
    }

    if (m_state->finished) {
        m_poll->stop();
        m_state->running = false;
        QString error;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            error = m_state->error;
        }
        const bool ok = m_state->ok.load();
        // The downloader was pointed at this state's flag when the install started; it points at
        // nothing again once the thread has finished, so a later install can set its own.
        setCancellationFlag(nullptr);
        m_progressLog->appendPlainText(ok ? tr("Installed: %1 file(s) downloaded, %2 already present")
                                               .arg(m_state->downloaded.load())
                                               .arg(m_state->skipped.load())
                                         : tr("Install failed: %1").arg(error));
        if (ok) {
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                m_installed = m_state->versionId.isEmpty() ? m_versions->currentData().toString() : m_state->versionId;
            }
            m_hint->setText(tr("%1 is installed").arg(m_installed));
            m_install->setEnabled(true);
            m_close->setText(tr("Done"));
            // Installed is the answer the dialog exists for, so it closes itself instead of waiting
            // to be dismissed. The short delay leaves the last line and the version name on screen
            // long enough to read, and Accept carries the new version id back to the page.
            m_autoClose->start();
        } else {
            m_hint->setText(tr("Install failed: %1").arg(error));
            m_install->setEnabled(true);
        }
        return;
    }

}
