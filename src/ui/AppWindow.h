// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The window: a rail down the left with the pages a launcher of this size needs, and the work of
 *  each page in its own class.
 */

#pragma once

#include <QMainWindow>

#include "core/Settings.h"
#include "theme/Theme.h"

class QListWidget;
class QLabel;
class QStackedWidget;
class LaunchPage;
class JavaPage;
class LogsPage;
class AccountsPage;
class ToolsPage;
class ModpacksPage;
class ContentPage;
class AboutPage;

namespace Komorebi {
class Button;
}

class AppWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit AppWindow(Komorebi::Theme theme, QWidget* parent = nullptr);

    /// Selects a page by position in the rail; used by the screenshot hook.
    void showPage(int index);

    /**
     * Says so when the remembered data directory is gone, and offers to go back to the default one.
     *
     * Called once, after the window is up. Without it a folder that was moved by hand looks like a
     * fresh install to the player, who then believes their versions and accounts were deleted.
     */
    void warnAboutMissingDataRoot();

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    void openSettings();
    /// Starts another copy of the launcher with the same arguments and quits this one.
    bool restartLauncher();
    void setStatus(const QString& text);
    void reloadEverything();

    Komorebi::Theme m_theme;
    Komorebi::Settings m_settings;

    QListWidget* m_rail = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_status = nullptr;

    LaunchPage* m_launchPage = nullptr;
    JavaPage* m_javaPage = nullptr;
    LogsPage* m_logsPage = nullptr;
    AccountsPage* m_accountsPage = nullptr;
    ToolsPage* m_toolsPage = nullptr;
    ModpacksPage* m_modpacksPage = nullptr;
    ContentPage* m_contentPage = nullptr;
    AboutPage* m_aboutPage = nullptr;
};
