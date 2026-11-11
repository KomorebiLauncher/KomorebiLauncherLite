// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The modpack page: search CurseForge, install what was found, or bring a zip of one.
 */

#pragma once

#include <QWidget>

#include <QList>
#include <QPair>
#include <QQueue>

#include <atomic>
#include <memory>

#include "core/content/CurseForge.h"
#include "ui/PackVersionDialog.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QProgressBar;
class QShowEvent;
class QTimer;

namespace Komorebi {
class Button;
class Combo;
}

class ModpacksPage : public QWidget {
    Q_OBJECT

   public:
    explicit ModpacksPage(QWidget* parent = nullptr);
    ~ModpacksPage() override;

   signals:
    void status(const QString& text);
    void installed(const QString& versionId);

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void search();
    void applySearch(int generation, const QList<Komorebi::CurseForgeProject>& projects, const QString& error);
    void installSelected();
    void importZip();
    void pollInstall();
    void cancelJob();
    void loadIcons(int generation, const QList<Komorebi::CurseForgeProject>& projects, int firstRow);
    void pumpIcons();
    void showSelectedVersion(int row);
    bool usingModrinth() const;
    void sourceChanged();
    /// The page of results after the ones already listed, appended instead of replacing them.
    void loadMore();
    void applyMore(int generation, const QList<Komorebi::CurseForgeProject>& projects, const QString& error);
    /// How the version picker asks this pack's source for its files.
    PackVersionDialog::Fetcher versionFetcher(const Komorebi::CurseForgeProject& project) const;
    struct Job;
    void startJob(const std::function<void(Job&)>& work, const QString& label);

    QLineEdit* m_query = nullptr;
    Komorebi::Combo* m_source = nullptr;
    QListWidget* m_results = nullptr;
    QLabel* m_hint = nullptr;
    QLabel* m_details = nullptr;
    QProgressBar* m_progress = nullptr;
    Komorebi::Button* m_install = nullptr;
    Komorebi::Button* m_cancel = nullptr;
    Komorebi::Button* m_import = nullptr;
    Komorebi::Button* m_search = nullptr;
    Komorebi::Button* m_more = nullptr;
    QTimer* m_poll = nullptr;
    QNetworkAccessManager* m_icons = nullptr;
    QQueue<QPair<int, QString>> m_iconQueue;
    int m_iconActive = 0;
    int m_iconGeneration = 0;

    QList<Komorebi::CurseForgeProject> m_projects;
    /// Shared with the thread that runs the install: the page can be closed while that thread is
    /// still writing progress into the job, and it must not write into freed memory.
    std::shared_ptr<Job> m_job;
    bool m_firstShown = false;
    /// A "load more" in flight, and whether the source has already said the list ended.
    bool m_moreRunning = false;
    bool m_moreExhausted = false;
    std::atomic<int> m_searchGeneration{ 0 };
    std::atomic<int> m_probeGeneration{ 0 };
};
