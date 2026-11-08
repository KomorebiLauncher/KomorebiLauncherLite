// SPDX-License-Identifier: GPL-3.0-only
/*
 *  One version's own settings. Anything left empty here follows the launcher's settings, which is
 *  what most versions want; the fields exist for the ones that do not.
 */

#pragma once

#include <QDialog>

#include "core/InstanceConfig.h"

class QLabel;
class QLineEdit;

namespace Komorebi {
class Button;
}

class InstanceSettingsDialog : public QDialog {
    Q_OBJECT

   public:
    InstanceSettingsDialog(const QString& versionId, const Komorebi::InstanceConfig& config, int launcherMemoryMb,
                           QWidget* parent = nullptr);

    Komorebi::InstanceConfig result() const { return m_result; }

   private:
    QString m_versionId;
    Komorebi::InstanceConfig m_result;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_memory = nullptr;
    QLineEdit* m_jvmArguments = nullptr;
    QLineEdit* m_gameArguments = nullptr;
    QLineEdit* m_description = nullptr;
};
