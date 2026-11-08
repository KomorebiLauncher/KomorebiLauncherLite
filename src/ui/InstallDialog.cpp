// SPDX-License-Identifier: GPL-3.0-only
#include "ui/InstallDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <mutex>
#include <thread>

#include "core/Paths.h"
#include "core/install/VersionInstaller.h"
#include "kit/Kit.h"

using namespace Komorebi;

struct InstallDialog::State {
    std::atomic<bool> running{ false };
    std::atomic<bool> finished{ false };
    std::atomic<bool> ok{ false };
    std::atomic<int> done{ 0 };
    std::atomic<int> total{ 0 };
    std::atomic<int> downloaded{ 0 };
    std::atomic<int> skipped{ 0 };
    std::mutex mutex;
    QString stage;
    QString current;
    QString error;
};

InstallDialog::InstallDialog(const VersionStore& store, QWidget* parent)
    : QDialog(parent), m_store(store), m_state(new State)
{
    setWindowTitle(tr("安装新版本"));
    setMinimumWidth(520);
    resize(620, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("选择一个版本"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    m_kind = new Komorebi::Combo(this);
    m_kind->addItem(tr("正式版"));
    m_kind->addItem(tr("快照"));
    m_kind->addItem(tr("全部"));
    row->addWidget(m_kind);

    m_versions = new Komorebi::Combo(this);
    row->addWidget(m_versions, 1);
    layout->addLayout(row);

    m_hint = new QLabel(tr("正在获取版本列表…"), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_progressLog = new QPlainTextEdit(this);
    m_progressLog->setReadOnly(true);
    m_progressLog->setMaximumBlockCount(200);
    m_progressLog->setPlaceholderText(tr("安装过程会显示在这里"));
    layout->addWidget(m_progressLog, 1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    m_install = new Komorebi::Button(tr("安装"), this);
    m_install->setPrimary(true);
    m_install->setEnabled(false);
    m_close = new Komorebi::Button(tr("关闭"), this);
    buttons->addWidget(m_install);
    buttons->addWidget(m_close);
    layout->addLayout(buttons);

    connect(m_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_install, &QPushButton::clicked, this, &InstallDialog::startInstall);
    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { loadManifest(); });

    m_poll = new QTimer(this);
    m_poll->setInterval(200);
    connect(m_poll, &QTimer::timeout, this, &InstallDialog::pollProgress);

    QTimer::singleShot(0, this, &InstallDialog::loadManifest);
}

void InstallDialog::loadManifest()
{
    if (m_state->running)
        return;

    m_hint->setText(tr("正在获取版本列表…"));
    m_install->setEnabled(false);
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QApplication::processEvents();

    QString error;
    const VersionManifest manifest = VersionManifest::fetch(&error);
    QApplication::restoreOverrideCursor();

    if (manifest.versions.isEmpty()) {
        m_hint->setText(tr("获取版本列表失败：%1").arg(error));
        return;
    }

    const int kind = m_kind->currentIndex();
    m_versions->clear();
    for (const auto& version : manifest.versions) {
        if (kind == 0 && version.type != QLatin1String("release"))
            continue;
        if (kind == 1 && version.type != QLatin1String("snapshot"))
            continue;
        m_versions->addItem(version.id, version.id);
    }

    m_hint->setText(tr("共 %1 个可选版本，最新正式版 %2，最新快照 %3")
                        .arg(m_versions->count())
                        .arg(manifest.latestRelease, manifest.latestSnapshot));
    m_install->setEnabled(m_versions->count() > 0);
}

void InstallDialog::startInstall()
{
    if (m_state->running)
        return;
    const QString id = m_versions->currentData().toString();
    if (id.isEmpty())
        return;

    m_state->running = true;
    m_state->finished = false;
    m_state->done = 0;
    m_state->total = 0;
    m_install->setEnabled(false);
    m_progressLog->clear();
    m_progressLog->appendPlainText(tr("开始安装 %1 …").arg(id));
    m_hint->setText(tr("正在安装 %1").arg(id));

    const QString dataRoot = Komorebi::dataRoot();
    State* state = m_state;
    std::thread([state, dataRoot, id] {
        VersionInstaller installer(dataRoot);
        installer.onProgress = [state](const InstallProgress& progress) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->stage = progress.stage;
            state->current = progress.current;
            state->done = progress.done;
            state->total = progress.total;
        };
        const InstallResult result = installer.install(id, true);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = result.failed.isEmpty() ? result.error : result.failed.first();
            state->downloaded = result.downloaded;
            state->skipped = result.skipped;
        }
        state->ok = result.ok;
        state->finished = true;
    }).detach();

    m_poll->start();
}

void InstallDialog::pollProgress()
{
    const int done = m_state->done.load();
    const int total = m_state->total.load();
    QString stage;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        stage = m_state->stage;
    }

    if (total > 0) {
        const int percent = done * 100 / total;
        m_hint->setText(tr("%1 %2%（%3/%4）").arg(stage).arg(percent).arg(done).arg(total));
    } else if (!stage.isEmpty()) {
        m_hint->setText(stage);
    }

    if (m_state->finished) {
        m_poll->stop();
        m_state->running = false;
        QString error;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            error = m_state->error;
        }
        const bool ok = m_state->ok.load();
        m_progressLog->appendPlainText(ok ? tr("安装完成：下载 %1 个文件，跳过 %2 个已存在的文件")
                                               .arg(m_state->downloaded.load())
                                               .arg(m_state->skipped.load())
                                         : tr("安装失败：%1").arg(error));
        if (ok) {
            m_installed = m_versions->currentData().toString();
            m_hint->setText(tr("%1 已安装").arg(m_installed));
            m_install->setEnabled(true);
            m_close->setText(tr("完成"));
        } else {
            m_hint->setText(tr("安装失败：%1").arg(error));
            m_install->setEnabled(true);
        }
        return;
    }

}
