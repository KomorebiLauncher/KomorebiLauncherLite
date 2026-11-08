// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AppWindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/Paths.h"
#include "core/launch/LaunchPlan.h"
#include "core/version/VersionJson.h"
#include "core/version/VersionResolver.h"
#include "kit/Kit.h"
#include "ui/InstallDialog.h"
#include "ui/SettingsDialog.h"
#include "core/Settings.h"

using namespace Komorebi;

namespace {

/// The rows in the version list: the id, and the one line that says what it is.
class VersionRow final : public QListWidgetItem {
   public:
    explicit VersionRow(const InstalledVersion& version)
        : QListWidgetItem(version.id)
    {
    }
};

QString formatSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 0));
}

}  // namespace

AppWindow::AppWindow(Theme theme, QWidget* parent)
    : QMainWindow(parent), m_theme(std::move(theme)), m_store(versionsRoot())
{
    setWindowTitle(QStringLiteral("木漏れ日 Lite"));
    setWindowIcon(QIcon(QStringLiteral(":/komorebi-lite-256.png")));
    resize(1040, 680);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- the bar across the top
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
    auto* folder = new Button(tr("Game folder"), bar);
    auto* refresh = new Button(tr("Refresh"), bar);
    auto* settingsButton = new Button(tr("Settings"), bar);
    barLayout->addWidget(install);
    barLayout->addWidget(folder);
    barLayout->addWidget(refresh);
    barLayout->addWidget(settingsButton);
    layout->addWidget(bar);

    // ---- the page: versions on the left, the selected one on the right
    auto* page = new QWidget(central);
    auto* pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(18, 6, 18, 18);
    pageLayout->setSpacing(16);

    m_list = new QListWidget(page);
    m_list->setFrameShape(QFrame::NoFrame);
    // The selection is a plum wash on paper, not the palette's solid highlight: the list sits on
    // the wallpaper and a filled block would read as a different colour entirely.
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 9px 12px; margin: 2px 0px; border-radius: 4px; color: #242e4c; }"
        "QListWidget::item:hover { background: rgba(250, 247, 240, 0.55); }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); color: #242e4c; }"));
    m_list->setMinimumWidth(280);
    m_list->setMaximumWidth(340);
    pageLayout->addWidget(m_list, 0);

    auto* detailPlate = new QFrame(page);
    detailPlate->setObjectName(QStringLiteral("detail"));
    detailPlate->setStyleSheet(QStringLiteral("QFrame#detail { background: rgba(250, 247, 240, 0.66);"
                                              " border: 1px solid rgba(201, 194, 180, 0.9); border-radius: 6px; }"));
    auto* detailLayout = new QVBoxLayout(detailPlate);
    detailLayout->setContentsMargins(22, 20, 22, 20);
    detailLayout->setSpacing(10);

    m_detailTitle = new QLabel(tr("Select a version"), detailPlate);
    QFont detailFont = m_detailTitle->font();
    detailFont.setPointSize(detailFont.pointSize() + 5);
    detailFont.setBold(true);
    m_detailTitle->setFont(detailFont);
    detailLayout->addWidget(m_detailTitle);

    m_detailBody = new QLabel(detailPlate);
    m_detailBody->setWordWrap(true);
    m_detailBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_detailBody);
    detailLayout->addStretch(1);

    // ---- the bar along the bottom: what the page is for
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(12);
    m_status = new QLabel(tr("Ready"), detailPlate);
    m_status->setObjectName(QStringLiteral("status"));
    bottom->addWidget(m_status, 1);

    m_playButton = new Button(tr("Play"), detailPlate);
    m_playButton->setPrimary(true);
    m_playButton->setEnabled(false);
    bottom->addWidget(m_playButton);
    detailLayout->addLayout(bottom);

    pageLayout->addWidget(detailPlate, 1);
    layout->addWidget(page, 1);

    setCentralWidget(central);

    connect(install, &QPushButton::clicked, this, &AppWindow::installVersion);
    connect(refresh, &QPushButton::clicked, this, &AppWindow::reload);
    connect(folder, &QPushButton::clicked, this, &AppWindow::openGameFolder);
    connect(settingsButton, &QPushButton::clicked, this, &AppWindow::openSettings);
    connect(m_playButton, &QPushButton::clicked, this, &AppWindow::launchSelected);
    connect(m_list, &QListWidget::currentRowChanged, this, &AppWindow::selectVersion);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { launchSelected(); });

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);

    reload();
}

AppWindow::~AppWindow()
{
    if (m_game && m_game->state() != QProcess::NotRunning) {
        // The game is not ours to kill when the window closes, but it should not be left without
        // its output file either.
        m_game->closeWriteChannel();
    }
}

void AppWindow::reload()
{
    m_versions = m_store.scan();

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const auto& version : m_versions) {
        auto* item = new VersionRow(version);
        item->setData(Qt::UserRole, version.id);
        m_list->addItem(item);
    }

    if (m_versions.isEmpty()) {
        m_selected.clear();
        m_detailTitle->setText(tr("No versions installed yet"));
        m_detailBody->setText(tr("Use Install a version in the top right corner to download one. It shows up here, and Play starts it."));
        m_playButton->setEnabled(false);
        setStatus(tr("Nothing installed"));
        return;
    }

    int row = 0;
    for (int i = 0; i < m_versions.size(); ++i) {
        if (m_versions.at(i).id == m_selected)
            row = i;
    }
    m_list->setCurrentRow(row);
    selectVersion(row);
}

void AppWindow::selectVersion(int row)
{
    if (row < 0 || row >= m_versions.size()) {
        m_selected.clear();
        m_playButton->setEnabled(false);
        return;
    }

    const auto& version = m_versions.at(row);
    m_selected = version.id;
    m_playButton->setEnabled(version.hasGameJar);

    m_detailTitle->setText(version.id);
    m_detailBody->setText(tr("Java %1 · %2\nInstalled at: %3\nGame files: %4\n\nThis version can be started.")
                               .arg(version.javaMajor > 0 ? QString::number(version.javaMajor) : tr("unknown"))
                               .arg(formatSize(version.size))
                               .arg(QDir::toNativeSeparators(version.path))
                               .arg(version.hasGameJar ? tr("complete") : tr("missing %1.jar").arg(version.id)));
    setStatus(version.hasGameJar ? tr("Ready") : tr("The game files of this version are missing; install it again"));
}

void AppWindow::launchSelected()
{
    if (m_selected.isEmpty())
        return;
    if (m_game && m_game->state() != QProcess::NotRunning) {
        setStatus(tr("The game is already running"));
        return;
    }

    const QString directory = versionDirectory(m_selected);
    const ResolvedVersion resolved = resolveVersion(versionsRoot(), m_selected);
    if (!resolved.ok()) {
        setStatus(tr("The description of this version cannot be read: %1").arg(resolved.error));
        return;
    }
    const VersionJson version = resolved.version;

    LaunchRequest request;
    request.version = version;
    request.librariesRoot = librariesRoot();
    request.assetsRoot = assetsRoot();
    request.nativesRoot = QDir(directory).filePath(QStringLiteral("natives"));
    request.gameDirectory = directory;
    request.clientJar = QDir(versionDirectory(resolved.baseId)).filePath(resolved.baseId + QStringLiteral(".jar"));
    request.playerName = QStringLiteral("Player");
    request.maxMemoryMb = m_settings.memoryMb;
    request.javaPath = m_settings.javaPath;
    request.tempDirectory = QDir(directory).filePath(QStringLiteral("tmp"));
    QDir().mkpath(request.tempDirectory);

    const LaunchPlan plan = buildLaunchPlan(request);
    if (!plan.valid() || !plan.missingFiles.isEmpty()) {
        setStatus(tr("Cannot start: %1").arg(plan.valid() ? tr("%1 file(s) are missing").arg(plan.missingFiles.size()) : plan.error));
        return;
    }

    if (!m_game) {
        m_game = new QProcess(this);
        m_game->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_game, &QProcess::started, this, [this] {
            setStatus(tr("Game started"));
            m_playButton->setEnabled(false);
        });
        connect(m_game, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
            setStatus(code == 0 ? tr("Game exited") : tr("The game exited with code %1").arg(code));
            m_playButton->setEnabled(true);
        });
        connect(m_game, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                setStatus(tr("Could not start: Java did not run"));
                m_playButton->setEnabled(true);
            }
        });
    }

    m_game->setWorkingDirectory(plan.workingDirectory);
    m_game->start(plan.program, plan.arguments);
    setStatus(tr("Starting…"));
}

void AppWindow::installVersion()
{
    InstallDialog dialog(m_store, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.installedId().isEmpty()) {
        m_selected = dialog.installedId();
        reload();
    }
}

void AppWindow::openSettings()
{
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_settings = dialog.result();
    m_settings.save();
    setStatus(tr("Settings saved (the language changes next time the launcher starts)"));
}

void AppWindow::openGameFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(dataRoot()));
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
    const QImage scaled = m_theme.wallpaper.scaled(size() * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
    painter.drawImage(QPointF((width() - scaled.width() / devicePixelRatioF()) / 2.0,
                              (height() - scaled.height() / devicePixelRatioF()) / 2.0),
                      scaled);
}
