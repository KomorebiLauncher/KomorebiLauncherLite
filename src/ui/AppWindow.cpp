// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AppWindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/Paths.h"
#include "kit/Kit.h"
#include "ui/SettingsDialog.h"
#include "ui/pages/LaunchPage.h"
#include "ui/pages/SimplePages.h"

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
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 10px 12px; margin: 2px 0px; border-radius: 4px; color: #242e4c; }"
        "QListWidget::item:hover { background: rgba(250, 247, 240, 0.55); }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); color: #242e4c; }"));
    m_rail->addItem(tr("Play"));
    m_rail->addItem(tr("Java"));
    m_rail->addItem(tr("Logs"));
    m_rail->addItem(tr("About"));
    bodyLayout->addWidget(m_rail, 0);

    m_pages = new QStackedWidget(body);
    m_launchPage = new LaunchPage(m_settings, m_pages);
    m_javaPage = new JavaPage(m_pages);
    m_logsPage = new LogsPage(m_pages);
    m_aboutPage = new AboutPage(m_pages);
    m_pages->addWidget(m_launchPage);
    m_pages->addWidget(m_javaPage);
    m_pages->addWidget(m_logsPage);
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
    connect(m_javaPage, &JavaPage::status, this, &AppWindow::setStatus);
    connect(m_logsPage, &LogsPage::status, this, &AppWindow::setStatus);
    connect(m_javaPage, &JavaPage::javaChosen, this, [this](const QString& path) {
        m_settings.javaPath = path;
        m_settings.save();
        m_launchPage->applySettings(m_settings);
    });

    m_rail->setCurrentRow(0);
    setStatus(tr("Ready"));
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
    setStatus(tr("Settings saved (the language changes next time the launcher starts)"));
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
