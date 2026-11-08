// SPDX-License-Identifier: GPL-3.0-only
#include "ui/LoginDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "kit/Kit.h"

using namespace Komorebi;

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Sign in with Microsoft"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Sign in with Microsoft"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* step1 = new QLabel(tr("Open this page in a browser:"), this);
    layout->addWidget(step1);

    auto* link = new QLabel(this);
    link->setOpenExternalLinks(true);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    link->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(QStringLiteral("https://www.microsoft.com/link")));
    layout->addWidget(link);

    auto* step2 = new QLabel(tr("and enter this code:"), this);
    layout->addWidget(step2);

    m_codeLabel = new QLabel(this);
    m_codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont codeFont = m_codeLabel->font();
    codeFont.setPointSize(codeFont.pointSize() + 10);
    codeFont.setBold(true);
    codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    m_codeLabel->setFont(codeFont);
    layout->addWidget(m_codeLabel);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    m_copy = new Button(tr("Copy the code"), this);
    m_copy->setPrimary(true);
    m_open = new Button(tr("Open the page"), this);
    auto* cancel = new Button(tr("Cancel"), this);
    buttons->addWidget(m_copy);
    buttons->addWidget(m_open);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    m_hint = new QLabel(tr("Waiting for the browser…"), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_code.userCode);
        m_copy->setText(tr("Copied"));
    });
    connect(m_open, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(m_code.verificationUri.isEmpty() ? QStringLiteral("https://www.microsoft.com/link")
                                                                       : m_code.verificationUri));
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &LoginDialog::poll);

    // Asking Microsoft for the code is a single short request; it can happen while the dialog is
    // being shown, before the player has read anything.
    QString error;
    m_code = MicrosoftAuth::startDeviceCode(&error);
    if (m_code.deviceCode.isEmpty()) {
        m_codeLabel->setText(QStringLiteral("—"));
        m_copy->setEnabled(false);
        m_hint->setText(tr("Could not start the sign-in: %1").arg(error));
        return;
    }

    m_codeLabel->setText(m_code.userCode);
    m_timer->start(m_code.interval * 1000);
    setHint(tr("Waiting for the browser… the code is good for %1 minutes.").arg(m_code.expiresIn / 60));
}

void LoginDialog::setHint(const QString& text)
{
    m_hint->setText(text);
}

void LoginDialog::poll()
{
    m_elapsed += m_code.interval;
    if (m_elapsed > m_code.expiresIn) {
        m_timer->stop();
        setHint(tr("The code expired. Close this and try again."));
        return;
    }

    const PollResult result = MicrosoftAuth::poll(m_code);
    switch (result.state) {
    case PollResult::State::Pending:
        setHint(tr("Waiting for the browser… %1: enter the code and approve.").arg(m_elapsed >= 60 ? tr("still waiting") : tr("waiting")));
        break;
    case PollResult::State::SlowDown:
        m_timer->setInterval(m_timer->interval() + 5000);
        setHint(tr("Waiting a little longer…"));
        break;
    case PollResult::State::Done:
        m_timer->stop();
        m_account = result.account;
        accept();
        break;
    case PollResult::State::Failed:
        m_timer->stop();
        setHint(tr("Sign-in failed: %1").arg(result.error));
        break;
    }
}
