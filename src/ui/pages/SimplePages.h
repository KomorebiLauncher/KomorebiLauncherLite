// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The small pages: the Java runtimes on this machine, the logs the launcher keeps, and who made it.
 */

#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTimer;

namespace Komorebi {
class Button;
}

/// Lists every Java the launcher can find, and lets one be copied into the settings.
class JavaPage : public QWidget {
    Q_OBJECT

   public:
    explicit JavaPage(QWidget* parent = nullptr);

    void reload();

   signals:
    void status(const QString& text);
    void javaChosen(const QString& path);

   private:
    QListWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
    Komorebi::Button* m_use = nullptr;
};

/// The launcher's own log files: open the folder, read one, clear them.
class LogsPage : public QWidget {
    Q_OBJECT

   public:
    explicit LogsPage(QWidget* parent = nullptr);

    void reload();

   signals:
    void status(const QString& text);

   protected:
    // The list is built once, so a log written while the launcher is running (the usual case: start
    // the game, then look) only shows up if the page reads the folder again when it is opened.
    void showEvent(QShowEvent* event) override;

   private:
    /// Reads the file the list points at into the viewer, from the end when it is long.
    void readSelected();
    /// Watches the file in the viewer while it is on screen: a running game keeps writing to it.
    void follow();

    QListWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
    QPlainTextEdit* m_view = nullptr;
    QTimer* m_follow = nullptr;
    QString m_path;
    qint64 m_loadedBytes = 0;
};

/// Microsoft accounts: sign one in, see who is signed in, sign out.
class AccountsPage : public QWidget {
    Q_OBJECT

   public:
    explicit AccountsPage(QWidget* parent = nullptr);

    void reload();

    /// The account a launch should use, empty when nobody is signed in.
    static QString activeName();

   signals:
    void status(const QString& text);

   private:
    void signIn();
    void refreshActiveMarks();
    void updateActions();
    QListWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
    Komorebi::Button* m_signOut = nullptr;
    bool m_loading = false;
};

/// The name, the version and the licence. Nothing else has to be here.
class AboutPage : public QWidget {
    Q_OBJECT

   public:
    explicit AboutPage(QWidget* parent = nullptr);
};
