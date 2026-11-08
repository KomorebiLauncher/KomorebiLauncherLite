// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The window, in the shape PCL has: the installed versions on the left, the selected one open on
 *  the right, and the launcher does its work from the bottom of the page.
 */

#pragma once

#include <QMainWindow>

#include "core/Settings.h"
#include "core/VersionStore.h"
#include "theme/Theme.h"

class QLabel;
class QListWidget;
class QProcess;
class QTimer;

namespace Komorebi {
class Button;
}

class AppWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit AppWindow(Komorebi::Theme theme, QWidget* parent = nullptr);
    ~AppWindow() override;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    void reload();
    void selectVersion(int row);
    void launchSelected();
    void installVersion();
    void openGameFolder();
    void openSettings();
    void setStatus(const QString& text);

    Komorebi::Theme m_theme;
    Komorebi::VersionStore m_store;
    Komorebi::Settings m_settings;

    QListWidget* m_list = nullptr;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailBody = nullptr;
    QLabel* m_status = nullptr;
    Komorebi::Button* m_playButton = nullptr;

    QList<Komorebi::InstalledVersion> m_versions;
    QString m_selected;

    QProcess* m_game = nullptr;
    QTimer* m_statusTimer = nullptr;
};
