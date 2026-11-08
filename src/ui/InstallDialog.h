// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The one dialog a player meets before playing anything: the list of versions the launcher can
 *  install. It fetches the list, shows it, and installs what was picked with a progress line.
 */

#pragma once

#include <QDialog>

#include "core/VersionStore.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace Komorebi {
class Button;
}
class QTimer;

class InstallDialog : public QDialog {
    Q_OBJECT

   public:
    explicit InstallDialog(const Komorebi::VersionStore& store, QWidget* parent = nullptr);

    /// The id that was installed, empty when nothing was.
    QString installedId() const { return m_installed; }

   private:
    void loadManifest();
    void startInstall();
    void pollProgress();

    const Komorebi::VersionStore& m_store;
    QComboBox* m_versions = nullptr;
    QComboBox* m_kind = nullptr;
    QLabel* m_hint = nullptr;
    QPlainTextEdit* m_progressLog = nullptr;
    Komorebi::Button* m_install = nullptr;
    Komorebi::Button* m_close = nullptr;
    QTimer* m_poll = nullptr;
    QString m_installed;

    struct State;
    State* m_state = nullptr;
};
