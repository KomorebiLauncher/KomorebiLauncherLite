// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The small pages: the Java runtimes on this machine, the logs the launcher keeps, and who made it.
 */

#pragma once

#include <QWidget>

class QLabel;
class QListWidget;

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
};

/// The launcher's own log files: open the folder, read one, clear them.
class LogsPage : public QWidget {
    Q_OBJECT

   public:
    explicit LogsPage(QWidget* parent = nullptr);

    void reload();

   signals:
    void status(const QString& text);

   private:
    QListWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
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
    QListWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
};

/// The name, the version and the licence. Nothing else has to be here.
class AboutPage : public QWidget {
    Q_OBJECT

   public:
    explicit AboutPage(QWidget* parent = nullptr);
};
