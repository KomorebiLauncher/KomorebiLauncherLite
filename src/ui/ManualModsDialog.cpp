// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ManualModsDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/Paths.h"
#include "kit/Kit.h"

using namespace Komorebi;

ManualModsDialog::ManualModsDialog(const QString& versionId, QWidget* parent)
    : QDialog(parent), m_versionId(versionId)
{
    setWindowTitle(tr("Mods to fetch by hand"));
    resize(680, 420);
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    // The one line the whole feature turns on, so it keeps the folder's own name and no full stop:
    // a sentence that ends in a file name reads like a path, not like prose.
    m_hint = new QLabel(tr("Download these files from their pages, then put them into the hokora folder"), this);
    m_hint->setWordWrap(true);
    m_hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_hint);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Mod"), tr("Version"), tr("File") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Nothing here is picked: the only thing to do with a row is click the link in it, and a
    // selected row paints the paper plum, which is where the link's own colour goes missing.
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(false);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; gridline-color: rgba(201, 194, 180, 0.5); }"
        "QTableWidget::item { padding-left: 8px; padding-right: 8px; color: #242e4c; border: none; }"
        "QHeaderView::section { background: rgba(242, 238, 230, 1); color: #242e4c;"
        " border: none; border-bottom: 1px solid rgba(201, 194, 180, 0.9); padding: 7px 8px; font-weight: 600; }"
        "QTableCornerButton::section { background: rgba(242, 238, 230, 1); border: none; }"));
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setShowGrid(false);
    m_table->setMinimumHeight(180);
    m_table->setMouseTracking(true);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != 0 || row < 0 || row >= m_mods.size())
            return;
        const QString page = m_mods.at(row).pageUrl;
        if (!page.isEmpty())
            QDesktopServices::openUrl(QUrl(page));
    });
    // The pointer turns into a hand over a name that opens a page, which is the only hint that the
    // cell does anything at all; the row itself is never selected, so the colour is all there is.
    connect(m_table, &QTableWidget::cellEntered, this, [this](int row, int column) {
        const bool link = column == 0 && row >= 0 && row < m_mods.size() && !m_mods.at(row).pageUrl.isEmpty();
        m_table->viewport()->setCursor(link ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    layout->addWidget(m_table, 1);

    m_rejected = new QLabel(this);
    m_rejected->setWordWrap(true);
    m_rejected->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rejected->hide();
    layout->addWidget(m_rejected);

    auto* buttons = new QHBoxLayout();
    auto* folder = new Button(tr("Open the hokora folder"), this);
    buttons->addWidget(folder);
    buttons->addStretch(1);
    m_look = new Button(tr("Look again"), this);
    buttons->addWidget(m_look);
    auto* close = new Button(tr("Close"), this);
    close->setPrimary(true);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(folder, &QPushButton::clicked, this, &ManualModsDialog::openFolder);
    connect(m_look, &QPushButton::clicked, this, [this] { look(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    // A player who leaves this dialog open while the browser downloads gets the files taken the
    // moment they land; the refresh button is for anyone who would rather be certain.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(400);
    connect(m_debounce, &QTimer::timeout, this, [this] { look(); });

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce->start(); });
    watch();

    look();
}

void ManualModsDialog::watch()
{
    // A watcher cannot watch a folder that does not exist yet, and the first person to open this
    // dialog has never heard of hokora, so it is made before it is watched.
    QDir().mkpath(hokoraDirectory());
    if (!m_watcher->directories().contains(hokoraDirectory()))
        m_watcher->addPath(hokoraDirectory());
}

void ManualModsDialog::look()
{
    const QString directory = versionDirectory(m_versionId);
    const HokoraHarvest harvest = harvestHokora(directory);
    m_mods = readManualMods(directory);
    showList(harvest);
    // The folder may have been deleted and remade while this dialog was open.
    watch();
    if (harvest.taken > 0)
        emit changed(m_versionId);
}

void ManualModsDialog::reload()
{
    m_table->setRowCount(m_mods.size());
    for (int row = 0; row < m_mods.size(); ++row) {
        const ManualMod& mod = m_mods.at(row);

        // The name is an ordinary cell, not a widget: a table draws its own items centred, while a
        // label put into a cell keeps the alignment it was built with and hung off the bottom of the
        // row. It is coloured and underlined like a link, and clicking the cell opens the page.
        auto* name = new QTableWidgetItem(mod.name);
        QFont nameFont = name->font();
        nameFont.setUnderline(!mod.pageUrl.isEmpty());
        name->setFont(nameFont);
        if (!mod.pageUrl.isEmpty()) {
            name->setForeground(m_table->palette().color(QPalette::Link));
            name->setToolTip(mod.pageUrl);
        }
        m_table->setItem(row, 0, name);

        auto* version = new QTableWidgetItem(mod.version);
        version->setToolTip(mod.fileName);
        m_table->setItem(row, 1, version);

        auto* file = new QTableWidgetItem(mod.fileName);
        file->setToolTip(mod.sha1.isEmpty() ? tr("The pack gave no hash for this file") : QStringLiteral("sha1 %1").arg(mod.sha1));
        m_table->setItem(row, 2, file);
    }
}

void ManualModsDialog::showList(const HokoraHarvest& harvest)
{
    reload();
    setWindowTitle(m_mods.isEmpty() ? tr("Nothing is missing") : tr("Mods to fetch by hand"));

    m_title->setText(m_mods.isEmpty() ? tr("Nothing is missing") : tr("%1 file(s) still to fetch").arg(m_mods.size()));
    m_hint->setText(m_mods.isEmpty()
                        ? tr("Every file is in place. The pack is complete")
                        : tr("Download these files from their pages, then put them into the hokora folder"));
    m_table->setVisible(!m_mods.isEmpty());
    m_look->setEnabled(!m_mods.isEmpty());

    if (harvest.rejected.isEmpty()) {
        m_rejected->clear();
        m_rejected->hide();
    } else {
        // Worth saying out loud: the file has the right name, so nothing else in the launcher would
        // ever mention it again, and the pack stays one mod short.
        m_rejected->setText(tr("Left in hokora, because the contents are not the file the pack asked for: %1")
                                .arg(harvest.rejected.join(QStringLiteral(", "))));
        m_rejected->show();
    }
}

void ManualModsDialog::openFolder()
{
    QDir().mkpath(hokoraDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(hokoraDirectory()));
}
