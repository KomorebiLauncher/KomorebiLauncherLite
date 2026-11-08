// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The settings a player actually changes: the language, how much memory the game may use, and
 *  which Java to run it with. Nothing here needs the launcher to be clever.
 */

#pragma once

#include <QDialog>

#include "core/Settings.h"

class QComboBox;
class QLineEdit;

namespace Komorebi {
class Button;
}

class SettingsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit SettingsDialog(const Komorebi::Settings& settings, QWidget* parent = nullptr);

    /// The settings as edited. It says what the launcher should use, not what is saved.
    Komorebi::Settings result() const { return m_result; }

   private:
    Komorebi::Settings m_result;
    QComboBox* m_language = nullptr;
    QLineEdit* m_java = nullptr;
    QLineEdit* m_memory = nullptr;
};
