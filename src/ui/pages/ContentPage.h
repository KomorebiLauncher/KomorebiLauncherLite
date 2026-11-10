// SPDX-License-Identifier: GPL-3.0-only
/*
 *  What is inside a version: its mods, resource packs, shaders, worlds and screenshots. The launcher
 *  can list them, turn a mod off without deleting it, delete one, and open the folder.
 */

#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;

namespace Komorebi {
class Button;
}

class ContentPage : public QWidget {
    Q_OBJECT

   public:
    explicit ContentPage(QWidget* parent = nullptr);

    void reload();
    void selectVersion(const QString& versionId);

   signals:
    void status(const QString& text);

   private:
    void loadFiles();
    void updateActions();
    void setEnabled(bool enabled);
    void removeSelected();
    void openFolder();

    QComboBox* m_version = nullptr;
    QComboBox* m_kind = nullptr;
    QListWidget* m_files = nullptr;
    QLabel* m_hint = nullptr;
    Komorebi::Button* m_toggle = nullptr;
    Komorebi::Button* m_remove = nullptr;
};
