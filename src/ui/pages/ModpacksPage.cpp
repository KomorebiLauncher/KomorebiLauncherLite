// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/ModpacksPage.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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

using namespace Komorebi;

struct ModpacksPage::Job {
    std::atomic<bool> done{ false };
    std::atomic<bool> ok{ false };
    std::atomic<int> downloaded{ 0 };
    std::atomic<int> skipped{ 0 };
    std::atomic<int> failed{ 0 };
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
    auto* searchButton = new Button(tr("Search"), this);
    searchButton->setPrimary(true);
    m_import = new Button(tr("Import a zip"), this);
    searchRow->addWidget(searchButton);
    searchRow->addWidget(m_import);
    layout->addLayout(searchRow);

    m_hint = new QLabel(tr("Pick a pack and press install; the game and its loader are installed as part of it."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_results = new QListWidget(this);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; padding: 6px; outline: none; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; color: #242e4c; outline: none; }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); }"));
    layout->addWidget(m_results, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    m_install = new Button(tr("Install"), this);
    m_install->setPrimary(true);
    m_install->setEnabled(false);
    bottom->addWidget(m_install);
    layout->addLayout(bottom);

    connect(searchButton, &QPushButton::clicked, this, &ModpacksPage::search);
    connect(m_query, &QLineEdit::returnPressed, this, &ModpacksPage::search);
    connect(m_install, &QPushButton::clicked, this, &ModpacksPage::installSelected);
    connect(m_import, &QPushButton::clicked, this, &ModpacksPage::importZip);
    connect(m_source, &QComboBox::currentIndexChanged, this, &ModpacksPage::sourceChanged);
    connect(m_results, &QListWidget::itemSelectionChanged, this, [this] {
        m_install->setEnabled(m_results->currentRow() >= 0);
    });

    m_poll = new QTimer(this);
    m_poll->setInterval(300);
    connect(m_poll, &QTimer::timeout, this, &ModpacksPage::pollInstall);

    emit status(tr("Search for a pack, or import one you already have"));
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
    m_results->clear();
    m_projects.clear();
    m_install->setEnabled(false);
}

void ModpacksPage::search()
{
    m_hint->setText(tr("Searching…"));
    QString error;
    if (usingModrinth())
        m_projects = Modrinth::searchModpacks(m_query->text().trimmed(), 30, &error);
    else
        m_projects = CurseForge::searchModpacks(m_query->text().trimmed(), 30, &error);

    m_results->clear();
    for (const auto& project : m_projects) {
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(project.name, project.slug.isEmpty() ? project.summary : project.summary));
        item->setToolTip(project.summary);
        m_results->addItem(item);
    }

    if (m_projects.isEmpty()) {
        m_hint->setText(tr("Nothing found: %1").arg(error));
        emit status(tr("Nothing found"));
        return;
    }
    m_hint->setText(tr("%1 pack(s). Pick one and press install.").arg(m_projects.size()));
    emit status(tr("%1 pack(s) found").arg(m_projects.size()));
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
            [this, project](Job& job) {
                QString error;
                const QList<CurseForgeFile> versions = Modrinth::versions(project.slug, {}, {}, &error);
                if (versions.isEmpty()) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.error = tr("the pack has no downloadable version: %1").arg(error);
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
                const QString zip = QDir(temp.path()).filePath(QStringLiteral("pack.zip"));
                {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.stage = tr("downloading the pack");
                    job.current = chosen.version;
                }
                if (!Modrinth::download(chosen.url, zip, &error)) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.error = tr("could not download the pack: %1").arg(error);
                    return;
                }

                const ModpackResult result = installMrpack(zip, true, [&job](const InstallProgress& progress) {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    job.stage = progress.stage;
                    job.current = progress.current;
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
        [this, project](Job& job) {
            QString error;
            const QList<CurseForgeFile> files = CurseForge::files(project.id, &error);
            if (files.isEmpty()) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.error = tr("the pack has no downloadable file: %1").arg(error);
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
            const QString zip = QDir(temp.path()).filePath(chosen.fileName);
            {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.stage = tr("downloading the pack");
                job.current = chosen.fileName;
            }
            if (!CurseForge::downloadFile(project.id, chosen.id, zip, &error)) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.error = tr("could not download the pack: %1").arg(error);
                return;
            }

            const ModpackResult result = installModpack(zip, true, [&job](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.stage = progress.stage;
                job.current = progress.current;
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
        [this, path](Job& job) {
            const ModpackResult result = installPack(path, true, [&job](const InstallProgress& progress) {
                std::lock_guard<std::mutex> lock(job.mutex);
                job.stage = progress.stage;
                job.current = progress.current;
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
    m_job = new Job;
    Job* job = m_job;
    m_hint->setText(tr("Installing %1…").arg(label));
    emit status(tr("Installing %1").arg(label));

    std::thread([job, work] {
        work(*job);
        job->done = true;
    }).detach();

    m_poll->start();
}

void ModpacksPage::pollInstall()
{
    if (!m_job)
        return;

    {
        std::lock_guard<std::mutex> lock(m_job->mutex);
        if (!m_job->stage.isEmpty())
            m_hint->setText(QStringLiteral("%1 · %2").arg(m_job->stage, m_job->current));
    }

    if (!m_job->done) {
        m_install->setEnabled(false);
        return;
    }

    m_poll->stop();
    const bool ok = m_job->ok;
    const QString error = m_job->error;
    const QString versionId = m_job->versionId;
    const int downloaded = m_job->downloaded;
    const int failed = m_job->failed;
    delete m_job;
    m_job = nullptr;
    m_install->setEnabled(m_results->currentRow() >= 0);

    if (!ok) {
        m_hint->setText(tr("Install failed: %1").arg(error));
        emit status(tr("Install failed: %1").arg(error));
        return;
    }

    m_hint->setText(tr("Installed as %1 (%2 files downloaded, %3 failed). It is on the Play page.").arg(versionId).arg(downloaded).arg(failed));
    emit status(tr("Installed %1").arg(versionId));
    emit installed(versionId);
}
