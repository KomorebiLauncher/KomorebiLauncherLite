// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The sign-in dialog: a code to type, a button that copies it, a link that opens the page, and a
 *  line that says what is happening while the player is in the browser.
 *
 *  It never blocks. The token endpoint is asked once per timer tick, so the window stays usable and
 *  the cancel button means something.
 */

#pragma once

#include <QDialog>

#include "core/auth/MicrosoftAuth.h"

class QLabel;
class QTimer;

namespace Komorebi {
class Button;
}

class LoginDialog : public QDialog {
    Q_OBJECT

   public:
    explicit LoginDialog(QWidget* parent = nullptr);

    /// The account, once the sign-in finished.
    Komorebi::MinecraftAccount account() const { return m_account; }

   private:
    void poll();
    void setHint(const QString& text);

    Komorebi::DeviceCode m_code;
    Komorebi::MinecraftAccount m_account;
    QTimer* m_timer = nullptr;
    QLabel* m_codeLabel = nullptr;
    QLabel* m_hint = nullptr;
    Komorebi::Button* m_copy = nullptr;
    Komorebi::Button* m_open = nullptr;
    int m_elapsed = 0;
};
