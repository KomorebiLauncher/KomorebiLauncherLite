// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The modpack page: search CurseForge, install what was found, or bring a zip of one.
 */

#pragma once

#include <QWidget>

#include <QList>

#include "core/content/CurseForge.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace Komorebi {
class Button;
}

class ModpacksPage : public QWidget {
    Q_OBJECT

   public:
    explicit ModpacksPage(QWidget* parent = nullptr);

   signals:
    void status(const QString& text);
    void installed(const QString& versionId);

   private:
    void search();
    void installSelected();
    void importZip();
    void pollInstall();
    struct Job;
    void startJob(const std::function<void(Job&)>& work, const QString& label);

    QLineEdit* m_query = nullptr;
    QListWidget* m_results = nullptr;
    QLabel* m_hint = nullptr;
    Komorebi::Button* m_install = nullptr;
    Komorebi::Button* m_import = nullptr;
    QTimer* m_poll = nullptr;

    QList<Komorebi::CurseForgeProject> m_projects;
    Job* m_job = nullptr;
};
