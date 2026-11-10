// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The page a player actually uses: the versions that are installed, the selected one opened, and
 *  the button that starts it.
 */

#pragma once

#include <QWidget>

#include "core/Settings.h"
#include "core/VersionStore.h"

class QLabel;
class QListWidget;
class QProcess;
class QProgressDialog;
class QShowEvent;
class QTimer;

namespace Komorebi {
class Button;
}

class LaunchPage : public QWidget {
    Q_OBJECT

   public:
    explicit LaunchPage(const Komorebi::Settings& settings, QWidget* parent = nullptr);

    void reload();
    void applySettings(const Komorebi::Settings& settings);
    void install();
    void selectVersion(const QString& versionId);

    /// The version the page has selected, empty when there is none.
    QString selectedVersion() const { return m_selected; }

   signals:
    void status(const QString& text);

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void selectRow(int row);
    void play();
    void refreshAccount();
    void openFolder();
    void completeFiles();
    void pollRepair();
    void exportScript();
    void editInstance();
    void renameVersion();
    void describeVersion();
    void removeVersion();

    Komorebi::Settings m_settings;
    Komorebi::VersionStore m_store;

    QListWidget* m_list = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_account = nullptr;
    QLabel* m_body = nullptr;
    QLabel* m_status = nullptr;
    Komorebi::Button* m_play = nullptr;

    QList<Komorebi::InstalledVersion> m_versions;
    QString m_selected;
    QProcess* m_game = nullptr;

    struct RepairState;
    RepairState* m_repair = nullptr;
    QProgressDialog* m_repairDialog = nullptr;
    QTimer* m_repairPoll = nullptr;
};
