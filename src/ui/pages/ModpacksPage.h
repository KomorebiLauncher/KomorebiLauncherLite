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

#include "core/content/CurseForge.h"

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
    void loadIcons(int generation, const QList<Komorebi::CurseForgeProject>& projects);
    void pumpIcons();
    void showSelectedVersion(int row);
    bool usingModrinth() const;
    void sourceChanged();
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
    QTimer* m_poll = nullptr;
    QNetworkAccessManager* m_icons = nullptr;
    QQueue<QPair<int, QString>> m_iconQueue;
    int m_iconActive = 0;
    int m_iconGeneration = 0;

    QList<Komorebi::CurseForgeProject> m_projects;
    Job* m_job = nullptr;
    bool m_firstShown = false;
    std::atomic<int> m_searchGeneration{ 0 };
    std::atomic<int> m_probeGeneration{ 0 };
};
