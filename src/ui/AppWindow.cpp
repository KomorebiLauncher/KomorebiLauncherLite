// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AppWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/Paths.h"
#include "kit/Kit.h"
#include "ui/SettingsDialog.h"
#include "ui/pages/LaunchPage.h"
#include "ui/pages/SimplePages.h"
#include "ui/pages/ToolsPage.h"
#include "ui/pages/ModpacksPage.h"
#include "ui/pages/ContentPage.h"

using namespace Komorebi;

AppWindow::AppWindow(Theme theme, QWidget* parent)
    : QMainWindow(parent), m_theme(std::move(theme)), m_settings(Settings::load())
{
    setWindowTitle(QStringLiteral("木漏れ日 Lite"));
    setWindowIcon(QIcon(QStringLiteral(":/komorebi-lite-256.png")));
    resize(1060, 700);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- top bar
    auto* bar = new QWidget(central);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(18, 12, 18, 12);
    barLayout->setSpacing(8);

    auto* crest = new QLabel(bar);
    crest->setPixmap(QIcon(QStringLiteral(":/komorebi-lite-256.png")).pixmap(24, 24));
    barLayout->addWidget(crest);

    auto* title = new QLabel(QStringLiteral("木漏れ日"), bar);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    barLayout->addWidget(title);
    barLayout->addStretch(1);

    auto* install = new Button(tr("Install a version"), bar);
    install->setPrimary(true);
    auto* settings = new Button(tr("Settings"), bar);
    barLayout->addWidget(install);
    barLayout->addWidget(settings);
    layout->addWidget(bar);

    // ---- rail + pages
    auto* body = new QWidget(central);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(18, 4, 18, 10);
    bodyLayout->setSpacing(16);

    m_rail = new QListWidget(body);
    m_rail->setFrameShape(QFrame::NoFrame);
    m_rail->setFixedWidth(150);
    m_rail->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { padding: 10px 12px; margin: 2px 0px; border-radius: 4px; color: #242e4c; }"
        "QListWidget::item:hover { background: rgba(250, 247, 240, 0.55); }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); color: #242e4c; }"));
    m_rail->addItem(tr("Play"));
    m_rail->addItem(tr("Modpacks"));
    m_rail->addItem(tr("Content"));
    m_rail->addItem(tr("Java"));
    m_rail->addItem(tr("Accounts"));
    m_rail->addItem(tr("Logs"));
    m_rail->addItem(tr("Tools"));
    m_rail->addItem(tr("About"));
    bodyLayout->addWidget(m_rail, 0);

    m_pages = new QStackedWidget(body);
    m_launchPage = new LaunchPage(m_settings, m_pages);
    m_modpacksPage = new ModpacksPage(m_pages);
    m_contentPage = new ContentPage(m_pages);
    m_javaPage = new JavaPage(m_pages);
    m_accountsPage = new AccountsPage(m_pages);
    m_logsPage = new LogsPage(m_pages);
    m_toolsPage = new ToolsPage(m_pages);
    m_aboutPage = new AboutPage(m_pages);
    m_pages->addWidget(m_launchPage);
    m_pages->addWidget(m_modpacksPage);
    m_pages->addWidget(m_contentPage);
    m_pages->addWidget(m_javaPage);
    m_pages->addWidget(m_accountsPage);
    m_pages->addWidget(m_logsPage);
    m_pages->addWidget(m_toolsPage);
    m_pages->addWidget(m_aboutPage);
    bodyLayout->addWidget(m_pages, 1);

    layout->addWidget(body, 1);

    // ---- status line
    auto* footer = new QWidget(central);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(18, 4, 18, 10);
    m_status = new QLabel(tr("Ready"), footer);
    footerLayout->addWidget(m_status, 1);
    layout->addWidget(footer);

    setCentralWidget(central);

    connect(m_rail, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(install, &QPushButton::clicked, this, [this] {
        m_launchPage->install();
        reloadEverything();
    });
    connect(settings, &QPushButton::clicked, this, &AppWindow::openSettings);
    connect(m_launchPage, &LaunchPage::status, this, &AppWindow::setStatus);
    connect(m_contentPage, &ContentPage::status, this, &AppWindow::setStatus);
    connect(m_modpacksPage, &ModpacksPage::status, this, &AppWindow::setStatus);
    connect(m_modpacksPage, &ModpacksPage::installed, this, [this](const QString& id) {
        m_launchPage->selectVersion(id);
        m_contentPage->reload();
        m_rail->setCurrentRow(0);
    });
    connect(m_javaPage, &JavaPage::status, this, &AppWindow::setStatus);
    connect(m_logsPage, &LogsPage::status, this, &AppWindow::setStatus);
    connect(m_accountsPage, &AccountsPage::status, this, &AppWindow::setStatus);
    connect(m_toolsPage, &ToolsPage::status, this, &AppWindow::setStatus);
    connect(m_javaPage, &JavaPage::javaChosen, this, [this](const QString& path) {
        m_settings.javaPath = path;
        m_settings.save();
        m_launchPage->applySettings(m_settings);
    });

    m_rail->setCurrentRow(0);
    setStatus(tr("Ready"));
}

void AppWindow::showPage(int index)
{
    if (index >= 0 && index < m_rail->count())
        m_rail->setCurrentRow(index);
}

void AppWindow::reloadEverything()
{
    m_launchPage->reload();
    m_javaPage->reload();
    m_logsPage->reload();
}

void AppWindow::openSettings()
{
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_settings = dialog.result();
    m_settings.save();
    m_launchPage->applySettings(m_settings);
    // The wallpaper is painted by this window, not by any child page: ask for a repaint so a new
    // opacity shows up at once.
    update();
    setStatus(tr("Settings saved (the language changes next time the launcher starts)"));

    // Everything else reads the data directory once, while starting, so a new one only counts after
    // a fresh start. Offering that start here saves the player from hunting for the shortcut.
    if (dialog.dataDirectoryChanged()) {
        const auto answer = QMessageBox::question(
            this, tr("Data directory"),
            tr("The data directory is now\n%1\n\nThe launcher has to start again to use it. Restart now?")
                .arg(QDir::toNativeSeparators(Komorebi::dataRoot())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes && restartLauncher())
            return;
        setStatus(tr("Restart the launcher to use the new data directory"));
    }
}

bool AppWindow::restartLauncher()
{
    // Same arguments as this start, so a debugging run keeps its page and flags.
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1)))
        return false;
    QCoreApplication::quit();
    return true;
}

void AppWindow::warnAboutMissingDataRoot()
{
    if (!Komorebi::chosenDataRootIsMissing()) {
        // A different way for the same thing to go wrong: the pointer that remembered a moved data
        // directory lives inside the default one, so deleting that folder forgets the choice without
        // leaving anything behind to notice.
        const QString forgotten = Komorebi::forgottenDataRoot();
        if (forgotten.isEmpty())
            return;

        const QString remembered = QDir::toNativeSeparators(forgotten);
        const QString fallback = QDir::toNativeSeparators(Komorebi::defaultDataRoot());
        if (!QFileInfo(forgotten).isDir()) {
            QMessageBox(QMessageBox::Warning, tr("Data directory"),
                        tr("This launcher was using\n%1\nfor its data, and the file that remembered it is "
                           "gone, along with the folder that file lived in.\n\n"
                           "That directory is not there either, so this start uses %2.")
                            .arg(remembered, fallback),
                        QMessageBox::Ok, this)
                .exec();
            return;
        }

        QMessageBox box(QMessageBox::Warning, tr("Data directory"),
                        tr("This launcher was using\n%1\nfor its data, and the file that remembered it — kept "
                           "in %2 — is gone. The folder that file lived in has probably been deleted.\n\n"
                           "The directory itself is still there, with everything in it. Go back to it?")
                            .arg(remembered, fallback),
                        QMessageBox::NoButton, this);
        auto* goBack = box.addButton(tr("Go back to it"), QMessageBox::AcceptRole);
        auto* startOver = box.addButton(tr("Start over with the default directory"), QMessageBox::RejectRole);
        box.setDefaultButton(goBack);
        box.exec();

        if (box.clickedButton() != goBack) {
            // "Start over" has to forget the second copy too, or every start would ask again.
            if (box.clickedButton() == startOver)
                Komorebi::setDataRoot(QString(), nullptr);
            return;
        }

        QString error;
        if (!Komorebi::setDataRoot(forgotten, &error)) {
            QMessageBox::warning(this, tr("Data directory"), tr("Could not remember that directory: %1").arg(error));
            return;
        }
        if (!restartLauncher())
            setStatus(tr("Restart the launcher to use that data directory"));
        return;
    }

    const QString missing = QDir::toNativeSeparators(Komorebi::dataRoot());
    const QString fallback = QDir::toNativeSeparators(Komorebi::defaultDataRoot());
    QMessageBox box(QMessageBox::Warning, tr("Data directory"),
                    tr("The data directory this launcher was using is not there:\n%1\n\n"
                       "It may have been moved or renamed, or its drive is not connected. Until it is back "
                       "this start has no versions, instances, accounts or saves to show.\n\n"
                       "Go back to the default directory, %2?")
                        .arg(missing, fallback),
                    QMessageBox::NoButton, this);
    auto* goBack = box.addButton(tr("Use the default directory"), QMessageBox::AcceptRole);
    auto* waitForIt = box.addButton(tr("Wait for it to come back"), QMessageBox::RejectRole);
    box.setDefaultButton(waitForIt);
    box.exec();
    if (box.clickedButton() != goBack)
        return;

    QString error;
    if (!Komorebi::setDataRoot(QString(), &error)) {
        QMessageBox::warning(this, tr("Data directory"), tr("Could not forget the missing directory: %1").arg(error));
        return;
    }
    if (!restartLauncher())
        setStatus(tr("Restart the launcher to use the default data directory"));
}

void AppWindow::setStatus(const QString& text)
{
    m_status->setText(text);
}

void AppWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (m_theme.wallpaper.isNull())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(qBound(0.0, m_settings.wallpaperOpacity / 100.0, 1.0));
    const QImage scaled = m_theme.wallpaper.scaled(size() * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
    painter.drawImage(QPointF((width() - scaled.width() / devicePixelRatioF()) / 2.0,
                              (height() - scaled.height() / devicePixelRatioF()) / 2.0),
                      scaled);
}
