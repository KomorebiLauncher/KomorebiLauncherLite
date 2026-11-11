// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The list of files a pack needs but nobody may download for the player.
 *
 *  A CurseForge pack can name a file whose author allows downloads only on the CurseForge site, and
 *  no launcher is allowed to fetch it. Installing the pack anyway and silently leaving those files
 *  out would leave a pack that looks installed and does not start, so the list is shown instead:
 *  what is missing, which version, and a link to the page of exactly that file. The player saves the
 *  files into `<data>/hokora`; the folder is watched, so closing this dialog is not required for
 *  them to be taken -- but looking again is also a button, for the impatient.
 */

#pragma once

#include <QDialog>
#include <QList>

#include "core/install/ManualMods.h"

class QFileSystemWatcher;
class QLabel;
class QTableWidget;
class QTimer;

namespace Komorebi {
class Button;
}

class ManualModsDialog : public QDialog {
    Q_OBJECT

   public:
    /// Opens on a version's list; `versionId` is what the version is called, not its folder.
    explicit ManualModsDialog(const QString& versionId, QWidget* parent = nullptr);

    /// Files still owed after the most recent look at hokora.
    int remaining() const { return m_mods.size(); }

   signals:
    /// Files were moved into the pack, so anything showing that pack's mods is now out of date.
    void changed(const QString& versionId);

   private:
    void look();
    void showList(const Komorebi::HokoraHarvest& harvest);
    void reload();
    void openFolder();
    void watch();
    QString m_versionId;
    QList<Komorebi::ManualMod> m_mods;
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
    QLabel* m_rejected = nullptr;
    QTableWidget* m_table = nullptr;
    Komorebi::Button* m_look = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_debounce = nullptr;
};
