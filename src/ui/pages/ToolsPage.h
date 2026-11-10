// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The small useful things: throw away what the game leaves behind, put a shortcut on the desktop,
 *  carry the settings to another machine.
 */

#pragma once

#include <QWidget>

class QLabel;
class QListWidget;

namespace Komorebi {
class Button;
}

class ToolsPage : public QWidget {
    Q_OBJECT

   public:
    explicit ToolsPage(QWidget* parent = nullptr);

   signals:
    void status(const QString& text);

   private:
    void cleanJunk();
    void createShortcut();
    void exportSettings();
    void importSettings();
    void downloadFile();
    /// Reports a download that ran on its own thread; always called on the GUI thread.
    void finishDownload(const QString& target, bool ok, const QString& error);
    void reload();

    Komorebi::Button* m_downloadButton = nullptr;
};
