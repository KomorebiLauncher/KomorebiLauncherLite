// SPDX-License-Identifier: GPL-3.0-only
#include "ui/PackVersionDialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <mutex>
#include <thread>
#include <utility>

#include "kit/Kit.h"

using namespace Komorebi;

namespace {

/// One page holds as much as the sources hand out in one request anyway.
constexpr int kPage = 50;

/// Bytes as a player reads them: a pack file is tens or hundreds of megabytes, and the exact byte
/// count next to a date is noise.
QString sizeText(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes >= 1024.0)
        return QStringLiteral("%1 GB").arg(QString::number(megabytes / 1024.0, 'f', 1));
    if (megabytes >= 100.0)
        return QStringLiteral("%1 MB").arg(QString::number(megabytes, 'f', 0));
    return QStringLiteral("%1 MB").arg(QString::number(megabytes, 'f', 1));
}

/// The day a file was published. The source writes an ISO timestamp; the hour of it means nothing
/// beside a version that may be a year old, and the date alone fits the column.
QString dateText(const QString& iso)
{
    if (iso.isEmpty())
        return QString();
    const QDateTime stamp = QDateTime::fromString(iso, Qt::ISODate);
    if (stamp.isValid())
        return stamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
    return iso.left(10);
}

bool isRelease(const CurseForgeFile& file)
{
    return file.releaseType == QLatin1String("1")
           || file.releaseType.compare(QLatin1String("release"), Qt::CaseInsensitive) == 0;
}

/// CurseForge numbers its release types, Modrinth names them; both end up as one word here.
QString typeText(const QString& releaseType)
{
    if (releaseType == QLatin1String("1")
        || releaseType.compare(QLatin1String("release"), Qt::CaseInsensitive) == 0)
        return PackVersionDialog::tr("release");
    if (releaseType == QLatin1String("2") || releaseType.compare(QLatin1String("beta"), Qt::CaseInsensitive) == 0)
        return PackVersionDialog::tr("beta");
    if (releaseType == QLatin1String("3") || releaseType.compare(QLatin1String("alpha"), Qt::CaseInsensitive) == 0)
        return PackVersionDialog::tr("alpha");
    return QString();
}

}  // namespace

/// What the fetch thread leaves for the dialog to pick up; the dialog never touches it unsynchronised.
struct PackVersionDialog::Page {
    std::mutex mutex;
    bool done = false;
    QList<CurseForgeFile> files;
    int total = 0;
    QString error;
};

PackVersionDialog::PackVersionDialog(const QString& packName, Fetcher fetcher, QWidget* parent)
    : QDialog(parent), m_fetcher(std::move(fetcher))
{
    setWindowTitle(tr("Choose the pack version"));
    resize(720, 440);
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(packName, this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setWordWrap(true);
    layout->addWidget(title);

    m_hint = new QLabel(tr("The newest file is usually the beta of the next release"), this);
    m_hint->setWordWrap(true);
    m_hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_hint);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({ tr("Version"), tr("Type"), tr("Published"), tr("Size"), tr("Game") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(false);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; gridline-color: rgba(201, 194, 180, 0.5); }"
        "QTableWidget::item { padding-left: 8px; padding-right: 8px; border: none; }"
        "QHeaderView::section { background: rgba(242, 238, 230, 1); color: #242e4c;"
        " border: none; border-bottom: 1px solid rgba(201, 194, 180, 0.9); padding: 7px 8px; font-weight: 600; }"
        "QTableCornerButton::section { background: rgba(242, 238, 230, 1); border: none; }"));
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    // Left-aligned headers over left-aligned cells, for the same reason the hokora list does it.
    for (int column = 0; column < 5; ++column)
        m_table->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The version takes the space it needs; the other four are short and the same width for every
    // row, so they share what is left instead of stretching a date across half the window.
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setShowGrid(false);
    m_table->setMinimumHeight(200);
    layout->addWidget(m_table, 1);

    auto* buttons = new QHBoxLayout();
    m_more = new Button(tr("Load more"), this);
    m_more->hide();
    buttons->addWidget(m_more);
    buttons->addStretch(1);
    auto* cancel = new Button(tr("Cancel"), this);
    buttons->addWidget(cancel);
    m_ok = new Button(tr("Install"), this);
    m_ok->setPrimary(true);
    m_ok->setEnabled(false);
    buttons->addWidget(m_ok);
    layout->addLayout(buttons);

    connect(m_more, &QPushButton::clicked, this, [this] { fetch(m_files.size()); });
    connect(cancel, &QPushButton::clicked, this, [this] { reject(); });
    connect(m_ok, &QPushButton::clicked, this, [this] { accept(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { accept(); });

    m_poll = new QTimer(this);
    m_poll->setInterval(80);
    connect(m_poll, &QTimer::timeout, this, [this] { applyPage(); });

    fetch(0);
}

void PackVersionDialog::fetch(int offset)
{
    if (m_loading || !m_fetcher)
        return;

    m_loading = true;
    m_more->setEnabled(false);
    m_hint->setText(tr("Looking for the versions…"));

    const std::shared_ptr<Page> page = std::make_shared<Page>();
    m_page = page;
    const Fetcher fetcher = m_fetcher;
    std::thread([fetcher, page, offset] {
        int total = 0;
        QString error;
        QList<CurseForgeFile> files = fetcher(offset, kPage, &total, &error);
        std::lock_guard<std::mutex> lock(page->mutex);
        page->files = files;
        page->total = total;
        page->error = error;
        page->done = true;
    }).detach();

    m_poll->start();
}

void PackVersionDialog::applyPage()
{
    if (!m_page)
        return;
    {
        std::lock_guard<std::mutex> lock(m_page->mutex);
        if (!m_page->done)
            return;
    }

    m_poll->stop();
    const std::shared_ptr<Page> page = m_page;
    m_page.reset();
    m_loading = false;

    if (!page->error.isEmpty()) {
        // A first page that never arrived leaves nothing to pick from, so the button that would ask
        // again is only shown when there is already a list to go with it.
        m_hint->setText(tr("Could not fetch the versions: %1").arg(page->error));
        m_more->setEnabled(true);
        m_more->setVisible(!m_files.isEmpty());
        updateButtons();
        return;
    }

    const QList<CurseForgeFile> files = page->files;
    if (!files.isEmpty()) {
        const int first = m_files.size();
        m_table->setRowCount(first + files.size());
        for (int i = 0; i < files.size(); ++i) {
            const CurseForgeFile& file = files.at(i);
            m_files.append(file);
            const int row = first + i;
            const QStringList cells{
                file.version.isEmpty() ? file.fileName : file.version,
                typeText(file.releaseType),
                dateText(file.date),
                sizeText(file.size),
                file.gameVersions.join(QLatin1Char(' ')),
            };
            for (int column = 0; column < cells.size(); ++column) {
                auto* item = new QTableWidgetItem(cells.at(column));
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                m_table->setItem(row, column, item);
            }
        }
        m_total = page->total;
        // A short page is the end of the list whether or not the source said how long it was.
        m_exhausted = files.size() < kPage || (m_total > 0 && m_files.size() >= m_total);
        if (m_total > 0)
            m_hint->setText(tr("Showing %1 of %2 files").arg(m_files.size()).arg(m_total));
        else
            m_hint->setText(tr("%1 files").arg(m_files.size()));
        if (first == 0)
            selectDefault();
    } else if (m_files.isEmpty()) {
        m_exhausted = true;
        m_hint->setText(tr("This pack has no downloadable file"));
    } else {
        m_exhausted = true;
    }

    updateButtons();
}

void PackVersionDialog::selectDefault()
{
    // The newest release, not the newest file: the file at the top is usually a beta of the next
    // version, and a player who opens this dialog wants the release unless they pick otherwise.
    int row = 0;
    for (int i = 0; i < m_files.size(); ++i) {
        if (isRelease(m_files.at(i))) {
            row = i;
            break;
        }
    }
    m_table->selectRow(row);
    m_table->setCurrentCell(row, 0);
}

void PackVersionDialog::updateButtons()
{
    const int row = m_table->currentRow();
    m_ok->setEnabled(!m_loading && row >= 0 && row < m_files.size());
    m_more->setVisible(!m_exhausted);
    m_more->setEnabled(!m_loading);
}

Komorebi::CurseForgeFile PackVersionDialog::selected() const
{
    // A row of -1 -- nothing loaded yet, or the list is on no row -- hands back an empty file, whose
    // empty name is what accept() reads as "there is nothing to install".
    return m_files.value(m_table ? m_table->currentRow() : -1);
}

void PackVersionDialog::accept()
{
    const Komorebi::CurseForgeFile file = selected();
    if (file.fileName.isEmpty())
        return;
    m_chosen = file;
    QDialog::accept();
}
