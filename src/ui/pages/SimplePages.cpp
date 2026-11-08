// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/SimplePages.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QApplication>
#include <QMessageBox>
#include "core/auth/AccountStore.h"
#include "ui/LoginDialog.h"
#include <QLabel>
#include <QListWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "core/JavaScan.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "kit/Kit.h"

using namespace Komorebi;

namespace {

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(font.pointSize() + 4);
    font.setBold(true);
    label->setFont(font);
    return label;
}

}  // namespace

// ---------------------------------------------------------------- Java

JavaPage::JavaPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* top = new QHBoxLayout();
    top->addWidget(heading(tr("Java"), this));
    top->addStretch(1);
    auto* refresh = new Button(tr("Look again"), this);
    auto* useIt = new Button(tr("Use this one"), this);
    useIt->setPrimary(true);
    top->addWidget(useIt);
    top->addWidget(refresh);
    layout->addLayout(top);

    m_hint = new QLabel(tr("A version starts with the runtime closest to what it asks for. Pick one here to pin it."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; padding: 6px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; color: #242e4c; }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); }"));
    layout->addWidget(m_list, 1);

    connect(refresh, &QPushButton::clicked, this, &JavaPage::reload);
    connect(useIt, &QPushButton::clicked, this, [this] {
        const auto* item = m_list->currentItem();
        if (!item)
            return;
        emit javaChosen(item->data(Qt::UserRole).toString());
        emit status(tr("Java pinned: %1").arg(item->data(Qt::UserRole).toString()));
    });

    reload();
}

void JavaPage::reload()
{
    m_list->clear();
    const QList<JavaRuntime> runtimes = JavaScan::find();
    for (const auto& runtime : runtimes) {
        const QString text = QStringLiteral("Java %1 · %2 · %3\n%4")
                                 .arg(runtime.major)
                                 .arg(runtime.vendor.isEmpty() ? tr("unknown vendor") : runtime.vendor,
                                      runtime.arch.isEmpty() ? QStringLiteral("?") : runtime.arch,
                                      QDir::toNativeSeparators(runtime.path));
        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, runtime.path);
        m_list->addItem(item);
    }

    if (runtimes.isEmpty()) {
        m_hint->setText(tr("No Java found. Install one, or set the path in the settings."));
        emit status(tr("No Java found"));
        return;
    }

    const Settings settings = Settings::load();
    if (!settings.javaPath.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == settings.javaPath) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }

    m_hint->setText(tr("%1 runtime(s) found.").arg(runtimes.size()));
    emit status(tr("%1 Java runtime(s)").arg(runtimes.size()));
}

// ---------------------------------------------------------------- logs

LogsPage::LogsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* top = new QHBoxLayout();
    top->addWidget(heading(tr("Logs"), this));
    top->addStretch(1);
    auto* open = new Button(tr("Open folder"), this);
    auto* copy = new Button(tr("Export"), this);
    auto* clear = new Button(tr("Clear"), this);
    top->addWidget(open);
    top->addWidget(copy);
    top->addWidget(clear);
    layout->addLayout(top);

    m_hint = new QLabel(tr("Every game start writes a log here."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; padding: 6px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; color: #242e4c; }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); }"));
    layout->addWidget(m_list, 1);

    connect(open, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(dataRoot()).filePath(QStringLiteral("logs"))));
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        const auto* item = m_list->currentItem();
        if (!item)
            return;
        const QString source = item->data(Qt::UserRole).toString();
        const QString target = QDir(QDir::homePath()).filePath(QFileInfo(source).fileName());
        if (QFile::copy(source, target))
            emit status(tr("Exported to %1").arg(QDir::toNativeSeparators(target)));
        else
            emit status(tr("Could not export %1").arg(QDir::toNativeSeparators(source)));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        QDir directory(QDir(dataRoot()).filePath(QStringLiteral("logs")));
        for (const auto& entry : directory.entryList(QDir::Files))
            QFile::remove(directory.filePath(entry));
        reload();
        emit status(tr("Logs cleared"));
    });

    reload();
}

void LogsPage::reload()
{
    m_list->clear();
    const QDir directory(QDir(dataRoot()).filePath(QStringLiteral("logs")));
    const auto entries = directory.entryInfoList(QDir::Files, QDir::Time);
    for (const auto& entry : entries) {
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 KB")
                                             .arg(entry.fileName(), entry.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                                  QString::number(entry.size() / 1024)));
        item->setData(Qt::UserRole, entry.absoluteFilePath());
        m_list->addItem(item);
    }
    m_hint->setText(entries.isEmpty() ? tr("No logs yet.") : tr("%1 log file(s).").arg(entries.size()));
    emit status(tr("%1 log file(s)").arg(entries.size()));
}

// ---------------------------------------------------------------- accounts

AccountsPage::AccountsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* top = new QHBoxLayout();
    top->addWidget(heading(tr("Accounts"), this));
    top->addStretch(1);
    auto* add = new Button(tr("Sign in with Microsoft"), this);
    add->setPrimary(true);
    auto* remove = new Button(tr("Sign out"), this);
    top->addWidget(add);
    top->addWidget(remove);
    layout->addLayout(top);

    m_hint = new QLabel(tr("A Microsoft account is what lets the game join servers that require one."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; padding: 6px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; color: #242e4c; }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); }"));
    layout->addWidget(m_list, 1);

    connect(add, &QPushButton::clicked, this, &AccountsPage::signIn);
    connect(remove, &QPushButton::clicked, this, [this] {
        const auto* item = m_list->currentItem();
        if (!item)
            return;
        AccountStore::remove(item->data(Qt::UserRole).toString());
        reload();
        emit status(tr("Signed out"));
    });

    reload();
}

void AccountsPage::reload()
{
    m_list->clear();
    const QList<MinecraftAccount> accounts = AccountStore::load();
    for (const auto& account : accounts) {
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(account.name, account.uuid));
        item->setData(Qt::UserRole, account.uuid);
        m_list->addItem(item);
    }
    if (accounts.isEmpty())
        emit status(tr("No account signed in"));
    else if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void AccountsPage::signIn()
{
    LoginDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        emit status(tr("Sign-in cancelled"));
        return;
    }

    const MinecraftAccount account = dialog.account();
    AccountStore::add(account);
    reload();
    emit status(tr("Signed in as %1").arg(account.name));
}
QString AccountsPage::activeName()
{
    const QList<MinecraftAccount> accounts = AccountStore::load();
    return accounts.isEmpty() ? QString() : accounts.first().name;
}
// ---------------------------------------------------------------- about

AboutPage::AboutPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    layout->addWidget(heading(QStringLiteral("木漏れ日 Lite"), this));

    auto* body = new QLabel(this);
    body->setWordWrap(true);
    body->setText(tr("A small Minecraft launcher: install a version, press Play.\n\n"
                     "GPL-3.0. The mark and the interface are original work.\n"
                     "Not an official Minecraft product; not approved by or associated with Mojang or Microsoft."));
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(body);

    // Who wrote what: the licence asks for the attribution, and a player deserves to know where
    // the account and modpack handling came from.
    auto* credits = new QLabel(this);
    credits->setWordWrap(true);
    credits->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    credits->setOpenExternalLinks(true);
    credits->setText(tr("Komorebi Lite is the light edition of KomorebiLauncher. The project's lineage is "
                        "<a href=\"https://github.com/PrismLauncher/PrismLauncher\">Prism Launcher</a> and, before it, "
                        "MultiMC -- this edition's code is written independently, following the public protocols those "
                        "projects also rely on. Details in NOTICE.md next to this launcher."));
    layout->addWidget(credits);

    auto* links = new QHBoxLayout();
    auto* source = new Button(tr("Source code"), this);
    auto* licence = new Button(tr("Licence"), this);
    links->addWidget(source);
    links->addWidget(licence);
    links->addStretch(1);
    layout->addLayout(links);
    layout->addStretch(1);

    connect(source, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/KomorebiLauncher/KomorebiLauncherLite")));
    });
    connect(licence, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.gnu.org/licenses/gpl-3.0.html")));
    });
}
