// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/ModpacksPage.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QStandardPaths>

#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <mutex>
#include <thread>

#include "core/Paths.h"
#include "core/content/Modrinth.h"
#include "core/install/ModpackInstaller.h"
#include "core/util/Scratch.h"
#include "kit/Kit.h"
#include "services/net/Fetcher.h"

using namespace Komorebi;

struct ModpacksPage::Job {
    std::atomic<bool> done{ false };
    std::atomic<bool> ok{ false };
    std::atomic<bool> cancelled{ false };
    std::atomic<int> downloaded{ 0 };
    std::atomic<int> skipped{ 0 };
    std::atomic<int> failed{ 0 };
    std::atomic<int> stepsDone{ 0 };
    std::atomic<int> stepsTotal{ 0 };
    std::mutex mutex;
    QString stage;
    QString current;
    QString error;
    QString versionId;
};

ModpacksPage::ModpacksPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("Modpacks"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    m_source = new Komorebi::Combo(this);
    m_source->addItem(tr("CurseForge"), QStringLiteral("curseforge"));
    m_source->addItem(tr("Modrinth"), QStringLiteral("modrinth"));
    m_query = new Komorebi::Field(this);
    m_query->setPlaceholderText(tr("Search CurseForge modpacks, or leave empty for the popular ones"));
    searchRow->addWidget(m_source);
    searchRow->addWidget(m_query, 1);
    m_search = new Button(tr("Search"), this);
    m_search->setPrimary(true);
    m_import = new Button(tr("Import a zip"), this);
    searchRow->addWidget(m_search);
    searchRow->addWidget(m_import);
    layout->addLayout(searchRow);

    m_hint = new QLabel(tr("Pick a pack and press install; the game and its loader are installed as part of it."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    // What the selected pack's newest version is, and which game and loader it is for: the list only
    // has room for a name, and "install" without knowing the version is a guess.
    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setStyleSheet(QStringLiteral("color: rgba(36, 46, 76, 0.72);"));
    layout->addWidget(m_details);

    // Installing a pack is minutes of work with no window of its own; without something moving the
    // page looks stuck. The bar tracks the stage that reports counts, the label says what it is doing.
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar { background: rgba(201, 194, 180, 0.45); border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: #8b243a; border-radius: 3px; }"));
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    m_results = new QListWidget(this);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setIconSize(QSize(28, 28));
    // A summary is longer than the row, so it is elided instead of growing a horizontal scrollbar
    // the list would otherwise need.
    m_results->setWordWrap(false);
    m_results->setTextElideMode(Qt::ElideRight);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_results->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; padding: 6px; outline: none; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; color: #242e4c; outline: none; }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); }"));
    layout->addWidget(m_results, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    m_cancel = new Button(tr("Cancel"), this);
    m_cancel->setVisible(false);
    bottom->addWidget(m_cancel);
    m_install = new Button(tr("Install"), this);
    m_install->setPrimary(true);
    m_install->setEnabled(false);
    bottom->addWidget(m_install);
    layout->addLayout(bottom);

    connect(m_search, &QPushButton::clicked, this, &ModpacksPage::search);
    connect(m_query, &QLineEdit::returnPressed, this, &ModpacksPage::search);
    connect(m_install, &QPushButton::clicked, this, &ModpacksPage::installSelected);
    connect(m_cancel, &QPushButton::clicked, this, &ModpacksPage::cancelJob);
    connect(m_import, &QPushButton::clicked, this, &ModpacksPage::importZip);
    connect(m_source, &QComboBox::currentIndexChanged, this, &ModpacksPage::sourceChanged);
    connect(m_results, &QListWidget::itemSelectionChanged, this, [this] {
        const int row = m_results->currentRow();
        m_install->setEnabled(row >= 0);
        showSelectedVersion(row);
    });

    m_poll = new QTimer(this);
    m_poll->setInterval(300);
    connect(m_poll, &QTimer::timeout, this, &ModpacksPage::pollInstall);

    // A development hook, like --page: a screenshot of this page wants a list and a selected row in
    // it, and a person clicking search is not available while one is being taken.
    const QString automaticSearch = qEnvironmentVariable("KOMOREBI_PACK_SEARCH");
    if (!automaticSearch.isEmpty()) {
        QTimer::singleShot(300, this, [this, automaticSearch] {
            m_query->setText(automaticSearch);
            search();
            if (m_results->count() > 0)
                m_results->setCurrentRow(0);
        });
    }

    emit status(tr("Search for a pack, or import one you already have"));
}

ModpacksPage::~ModpacksPage()
{
    // The window can be closed while a pack is being installed, and the page is a child of it. The
    // install thread keeps its own reference to the job, so the job outlives the page instead of
    // being written into after the page is gone; what has to happen here is that the process-wide
    // cancellation pointer stops aiming at a job this page is dropping, and that nothing is left
    // polling a widget that is about to be destroyed.
    if (m_job) {
        m_job->cancelled = true;
        setCancellationFlag(nullptr);
        m_job.reset();
    }
}

void ModpacksPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Opening the page answers with the popular packs, so it starts as a list instead of an empty
    // frame. A query typed later replaces the results.
    if (!m_firstShown) {
        m_firstShown = true;
        search();
    }
}

bool ModpacksPage::usingModrinth() const
{
    return m_source && m_source->currentData().toString() == QLatin1String("modrinth");
}

void ModpacksPage::sourceChanged()
{
    m_query->setPlaceholderText(usingModrinth()
                                    ? tr("Search Modrinth modpacks, or leave empty for the popular ones")
                                    : tr("Search CurseForge modpacks, or leave empty for the popular ones"));
    // The icons of the previous source are still arriving; asking for a new generation makes them
    // land in the bit bucket instead of on this source's rows.
    ++m_searchGeneration;
    m_iconQueue.clear();
    m_results->clear();
    m_details->clear();
    m_projects.clear();
    m_install->setEnabled(false);
}

void ModpacksPage::search()
{
    const int generation = ++m_searchGeneration;
    const QString query = m_query->text().trimmed();
    const bool modrinth = usingModrinth();

    m_hint->setText(tr("Searching…"));
    m_search->setEnabled(false);
    m_install->setEnabled(false);
    emit status(tr("Searching"));

    // A search answers in a second or two, or in sixty when the service does not: on the window's
    // own thread the launcher was frozen for exactly that long, with "Searching…" never painted.
    QPointer<ModpacksPage> guard(this);
    std::thread([guard, generation, query, modrinth] {
        QString error;
        const QList<CurseForgeProject> projects = modrinth ? Modrinth::searchModpacks(query, 30, &error)
                                                           : CurseForge::searchModpacks(query, 30, &error);
        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, projects, error] {
                if (guard)
                    guard->applySearch(generation, projects, error);
            },
            Qt::QueuedConnection);
    }).detach();
}

void ModpacksPage::applySearch(int generation, const QList<Komorebi::CurseForgeProject>& projects,
                               const QString& error)
{
    // A newer search (or a different source) has already asked for its own results.
    if (generation != m_searchGeneration.load())
        return;

    m_search->setEnabled(true);
    m_projects = projects;

    m_results->clear();
    m_details->clear();
    for (const auto& project : m_projects) {
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(project.name, project.summary));
        item->setToolTip(project.summary);
        m_results->addItem(item);
    }

    // The logos are urls the sources handed out; fetch them after the list is up, so a slow image
    // never holds up the results. This runs on the window's own event loop: a QNetworkAccessManager
    // belongs to the thread that made it, and a worker thread here would simply never get an answer.
    loadIcons(generation, m_projects);

    if (m_projects.isEmpty()) {
        m_hint->setText(error.isEmpty() ? tr("Nothing found. Try another word.")
                                        : tr("Nothing found: %1").arg(error));
        emit status(tr("Nothing found"));
        return;
    }
    m_hint->setText(tr("%1 pack(s). Pick one and press install.").arg(m_projects.size()));
    emit status(tr("%1 pack(s) found").arg(m_projects.size()));
}

void ModpacksPage::loadIcons(int generation, const QList<Komorebi::CurseForgeProject>& projects)
{
    if (!m_icons) {
        m_icons = new QNetworkAccessManager(this);
        m_icons->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    m_iconQueue.clear();
    m_iconGeneration = generation;
    for (int index = 0; index < projects.size(); ++index) {
        if (!projects.at(index).logoUrl.isEmpty())
            m_iconQueue.enqueue({ index, projects.at(index).logoUrl });
    }
    pumpIcons();
}

void ModpacksPage::pumpIcons()
{
    // Four at a time: enough that thirty logos arrive quickly, few enough to stay polite.
    while (!m_iconQueue.isEmpty() && m_iconActive < 4) {
        const QPair<int, QString> job = m_iconQueue.dequeue();
        QNetworkRequest request{ QUrl(job.second) };
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) KomorebiLite/0.1"));
        QNetworkReply* reply = m_icons->get(request);
        ++m_iconActive;
        connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
            --m_iconActive;
            if (reply->error() == QNetworkReply::NoError && m_iconGeneration == m_searchGeneration.load()
                && job.first < m_results->count()) {
                QImage image;
                if (image.loadFromData(reply->readAll())) {
                    const QImage scaled = image.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    m_results->item(job.first)->setIcon(QIcon(QPixmap::fromImage(scaled)));
                }
            }
            reply->deleteLater();
            pumpIcons();
        });
    }
}

void ModpacksPage::showSelectedVersion(int row)
{
    if (row < 0 || row >= m_projects.size()) {
        m_details->clear();
        return;
    }

    const CurseForgeProject project = m_projects.at(row);
    const bool modrinth = usingModrinth();
    const int generation = ++m_probeGeneration;
    m_details->setText(tr("Looking up the newest version…"));

    // The page can be gone by the time the services answer; a reference to a deleted widget is what
    // the process-wide network manager would then be writing into.
    QPointer<ModpacksPage> guard(this);
    std::thread([guard, generation, modrinth, project] {
        QString error;
        const QList<CurseForgeFile> versions = modrinth ? Modrinth::versions(project.slug, {}, {}, &error)
                                                        : CurseForge::files(project.id, &error);
        // The newest release, otherwise the newest of all: the list is ordered newest first. Taking
        // the last one the loop walked over made "the newest version" the oldest the API returned.
        CurseForgeFile newest;
        if (!versions.isEmpty()) {
            newest = versions.first();
            for (const auto& candidate : versions) {
                // CurseForge numbers its release types, Modrinth spells them out.
                if (candidate.releaseType == QLatin1String("release") || candidate.releaseType == QLatin1String("1")) {
                    newest = candidate;
                    break;
                }
            }
        }

        QString text;
        if (!versions.isEmpty()) {
            const QString label = newest.version.isEmpty() ? newest.displayName : newest.version;
            QStringList minecraft;
            QStringList loaders;
            for (const QString& value : newest.gameVersions) {
                if (value.isEmpty())
                    continue;
                if (value.at(0).isDigit())
                    minecraft << value;
                else
                    loaders << value;
            }
            QStringList bits{ label };
            bits << tr("Minecraft %1").arg(minecraft.isEmpty() ? tr("any") : minecraft.join(QStringLiteral(", ")));
            if (!loaders.isEmpty())
                bits << loaders.join(QStringLiteral(", "));
            // CurseForge numbers its release types; Modrinth spells them out.
            QString type = newest.releaseType;
            if (type == QLatin1String("1"))
                type = QStringLiteral("release");
            else if (type == QLatin1String("2"))
                type = QStringLiteral("beta");
            else if (type == QLatin1String("3"))
                type = QStringLiteral("alpha");
            if (!type.isEmpty() && type != QLatin1String("release"))
                bits << type;
            text = bits.join(QStringLiteral(" · "));
        } else {
            text = tr("The newest version could not be looked up: %1")
                       .arg(error.isEmpty() ? tr("no answer") : error);
        }

        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, text] {
                if (!guard || generation != guard->m_probeGeneration.load())
                    return;
                guard->m_details->setText(text);
            },
            Qt::QueuedConnection);
    }).detach();
}

void ModpacksPage::installSelected()
{
    const int row = m_results->currentRow();
    if (row < 0 || row >= m_projects.size() || m_job)
        return;

    const CurseForgeProject project = m_projects.at(row);
    m_install->setEnabled(false);

    if (usingModrinth()) {
        // A Modrinth version carries its own file url; the pack itself is the same .mrpack the
        // importer accepts, so the two paths end in the same installer.
        startJob(
            [project](Job& job) {
                QString error;
                const QList<CurseForgeFile> versions = Modrinth::versions(project.slug, {}, {}, &error);
                if (versions.isEmpty()) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.error = ModpacksPage::tr("the pack has no downloadable version: %1").arg(error);
                    return;
                }

                CurseForgeFile chosen = versions.first();
                for (const auto& file : versions) {
                    if (file.releaseType == QLatin1String("release")) {
                        chosen = file;
                        break;
                    }
                }

                ScratchDirectory temp("pack");
                if (!temp.isValid()) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.error = ModpacksPage::tr("no place to unpack the pack");
                    return;
                }
                const QString zip = QDir(temp.path()).filePath(QStringLiteral("pack.zip"));
                {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.stage = ModpacksPage::tr("downloading the pack");
                    job.current = chosen.version;
                }
                if (!Modrinth::download(chosen.url, zip, chosen.sha1, &error)) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.error = ModpacksPage::tr("could not download the pack: %1").arg(error);
                    return;
                }

                const ModpackResult result = installMrpack(zip, true, [&job](const InstallProgress& progress) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.stage = progress.stage;
                    job.current = progress.current;
                    job.stepsDone = progress.done;
                    job.stepsTotal = progress.total;
                });
                std::lock_guard<std::mutex> lock(job.mutex);
                job.ok = result.ok;
                job.error = result.error;
                job.versionId = result.versionId;
                job.downloaded = result.downloaded;
                job.skipped = result.skipped;
                job.failed = result.failed.size();
            },
            project.name);
        return;
    }

    startJob(
        [project](Job& job) {
            QString error;
            const QList<CurseForgeFile> files = CurseForge::files(project.id, &error);
            if (files.isEmpty()) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.error = ModpacksPage::tr("the pack has no downloadable file: %1").arg(error);
                return;
            }

            // The newest file the author marked as a release, otherwise the newest of all.
            CurseForgeFile chosen = files.first();
            for (const auto& file : files) {
                if (file.releaseType == QLatin1String("1")) {
                    chosen = file;
                    break;
                }
            }

            ScratchDirectory temp("pack");
            if (!temp.isValid()) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.error = ModpacksPage::tr("no place to unpack the pack");
                return;
            }
            const QString zip = QDir(temp.path()).filePath(chosen.fileName);
            {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.stage = ModpacksPage::tr("downloading the pack");
                job.current = chosen.fileName;
            }
            if (!CurseForge::downloadFile(project.id, chosen.id, zip, chosen.sha1, &error)) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.error = ModpacksPage::tr("could not download the pack: %1").arg(error);
                return;
            }

            const ModpackResult result = installModpack(zip, true, [&job](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.stage = progress.stage;
                job.current = progress.current;
                job.stepsDone = progress.done;
                job.stepsTotal = progress.total;
            });
            std::lock_guard<std::mutex> lock(job.mutex);
            job.ok = result.ok;
            job.error = result.error;
            job.versionId = result.versionId;
            job.downloaded = result.downloaded;
            job.skipped = result.skipped;
            job.failed = result.failed.size();
        },
        project.name);
}

void ModpacksPage::importZip()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import a modpack"),
                                                      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                      tr("Modpack (*.zip)"));
    if (path.isEmpty() || m_job)
        return;

    m_install->setEnabled(false);
    startJob(
        [path](Job& job) {
            const ModpackResult result = installPack(path, true, [&job](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.stage = progress.stage;
                job.current = progress.current;
                job.stepsDone = progress.done;
                job.stepsTotal = progress.total;
            });
            std::lock_guard<std::mutex> lock(job.mutex);
            job.ok = result.ok;
            job.error = result.error;
            job.versionId = result.versionId;
            job.downloaded = result.downloaded;
            job.failed = result.failed.size();
        },
        path);
}

void ModpacksPage::startJob(const std::function<void(Job&)>& work, const QString& label)
{
    // The job is shared with the thread below rather than owned by the page: closing the window in
    // the middle of an install handed that thread a pointer to memory the page had just deleted.
    m_job = std::make_shared<Job>();
    const std::shared_ptr<Job> job = m_job;
    m_hint->setText(tr("Installing %1…").arg(label));
    m_progress->setRange(0, 0);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_install->setEnabled(false);
    m_search->setEnabled(false);
    m_cancel->setEnabled(true);
    m_cancel->setVisible(true);
    emit status(tr("Installing %1").arg(label));

    // Downloads look at this flag; the button below sets it and the install rolls back out.
    setCancellationFlag(&job->cancelled);

    std::thread([job, work] {
        work(*job);
        job->done = true;
    }).detach();

    m_poll->start();
}

void ModpacksPage::cancelJob()
{
    if (!m_job)
        return;
    m_job->cancelled = true;
    m_cancel->setEnabled(false);
    m_hint->setText(tr("Cancelling…"));
    emit status(tr("Cancelling the install"));
}

void ModpacksPage::pollInstall()
{
    if (!m_job)
        return;

    {
        std::lock_guard<std::mutex> lock(m_job->mutex);
        if (!m_job->stage.isEmpty()) {
            const int total = m_job->stepsTotal.load();
            const int done = m_job->stepsDone.load();
            if (total > 0) {
                m_progress->setRange(0, 100);
                m_progress->setValue(done * 100 / total);
                // Most stages report counts without naming a file; the line used to read "files ·  (3/120)".
                m_hint->setText(m_job->current.isEmpty()
                                    ? tr("%1 (%2/%3)").arg(m_job->stage).arg(done).arg(total)
                                    : tr("%1 · %2 (%3/%4)").arg(m_job->stage, m_job->current).arg(done).arg(total));
            } else {
                // A stage without counts (copying overrides, asking for metadata) still gets a bar
                // that moves: an indeterminate one says the job is alive without inventing numbers.
                m_progress->setRange(0, 0);
                m_hint->setText(m_job->current.isEmpty() ? m_job->stage
                                                         : QStringLiteral("%1 · %2").arg(m_job->stage, m_job->current));
            }
        }
    }

    if (!m_job->done) {
        m_install->setEnabled(false);
        return;
    }

    m_poll->stop();
    // Everything the job has to say is read under its lock: the strings are written by the install
    // thread, and reading them bare raced with it. Only then let the job go -- that thread has
    // finished, and the page's reference is the last one.
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString versionId;
    int downloaded = 0;
    int failed = 0;
    {
        std::lock_guard<std::mutex> lock(m_job->mutex);
        ok = m_job->ok;
        cancelled = m_job->cancelled.load();
        error = m_job->error;
        versionId = m_job->versionId;
        downloaded = m_job->downloaded;
        failed = m_job->failed;
    }
    m_job.reset();
    setCancellationFlag(nullptr);
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    m_cancel->setVisible(false);
    m_install->setEnabled(m_results->currentRow() >= 0);
    m_search->setEnabled(true);

    // A finished install is a finished install: the cancel button may have been pressed in the last
    // second of it, and reporting "cancelled" left the new pack invisible on the Play page.
    if (ok) {
        m_hint->setText(tr("Installed as %1 (%2 files downloaded, %3 failed). It is on the Play page.").arg(versionId).arg(downloaded).arg(failed));
        emit status(tr("Installed %1").arg(versionId));
        emit installed(versionId);
        return;
    }

    if (cancelled) {
        m_hint->setText(tr("Install cancelled. What was already downloaded is kept."));
        emit status(tr("Install cancelled"));
        return;
    }

    m_hint->setText(tr("Install failed: %1").arg(error));
    emit status(tr("Install failed: %1").arg(error));
}
