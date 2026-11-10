// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Everything a player can change about the launcher, on one page: how the game starts, and how
 *  the launcher looks.
 */

#pragma once

#include <QDialog>

#include "core/Settings.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace Komorebi {
class Button;
class Slider;
}

class SettingsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit SettingsDialog(const Komorebi::Settings& settings, QWidget* parent = nullptr);

    /// The settings as edited. It says what the launcher should use, not what is saved.
    Komorebi::Settings result() const { return m_result; }

    /// Whether the data directory was switched: the launcher has to start again to use it.
    bool dataDirectoryChanged() const { return m_dataDirectoryChanged; }

   private:
    /// Applies the chosen data directory, copying the old one over when the player asked for it.
    /// Returns false when the dialog should stay open.
    bool switchDataDirectory(const QString& target, QString* error);

    Komorebi::Settings m_result;

    QString m_dataDirectory;
    bool m_dataDirectoryChanged = false;

    QComboBox* m_language = nullptr;
    QComboBox* m_priority = nullptr;
    QLineEdit* m_memory = nullptr;
    QLineEdit* m_width = nullptr;
    QLineEdit* m_height = nullptr;
    QLineEdit* m_java = nullptr;
    QLineEdit* m_jvmArguments = nullptr;
    QLineEdit* m_gameArguments = nullptr;
    QLineEdit* m_preLaunch = nullptr;
    QCheckBox* m_lockMemory = nullptr;
    QCheckBox* m_highPerformanceGpu = nullptr;
    QCheckBox* m_legacyFix = nullptr;
    QCheckBox* m_unsafeAgent = nullptr;
    Komorebi::Slider* m_wallpaperOpacity = nullptr;
    QComboBox* m_downloadSite = nullptr;
    QLineEdit* m_mirrorBase = nullptr;
    QLineEdit* m_concurrency = nullptr;
    QLineEdit* m_speedLimit = nullptr;
    QLineEdit* m_msaClientId = nullptr;
    QLineEdit* m_dataDirectoryField = nullptr;
};
