// SPDX-License-Identifier: GPL-3.0-only
#include "ui/pages/ContentPage.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

#include "core/Paths.h"
#include "core/VersionStore.h"
#include "core/install/ManualMods.h"
#include "kit/Kit.h"
#include "ui/ManualModsDialog.h"

using namespace Komorebi;

namespace {

/// The folders a version keeps its content in, and whether turning things off means renaming them.
struct Kind {
    const char* folder;
    const char* label;  // translated at the call site
    bool toggleable;    // mods are switched off by renaming; worlds are not
};

constexpr Kind kKinds[] = {
    { "mods", QT_TRANSLATE_NOOP("ContentPage", "Mods"), true },
    { "resourcepacks", QT_TRANSLATE_NOOP("ContentPage", "Resource packs"), true },
    { "shaderpacks", QT_TRANSLATE_NOOP("ContentPage", "Shader packs"), true },
    { "saves", QT_TRANSLATE_NOOP("ContentPage", "Worlds"), false },
    { "screenshots", QT_TRANSLATE_NOOP("ContentPage", "Screenshots"), false },
};

QString disabledSuffix()
{
    return QStringLiteral(".disabled");
}

}  // namespace

ContentPage::ContentPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("Content"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* choosers = new QHBoxLayout();
    choosers->setSpacing(8);
    m_version = new Komorebi::Combo(this);
    m_version->setMinimumWidth(260);
    choosers->addWidget(m_version, 1);
    m_kind = new Komorebi::Combo(this);
    for (const auto& kind : kKinds)
        m_kind->addItem(tr(kind.label), QString::fromLatin1(kind.folder));
    choosers->addWidget(m_kind, 1);

    auto* refresh = new Button(tr("Refresh"), this);
    choosers->addWidget(refresh);
    layout->addLayout(choosers);

    m_hint = new QLabel(tr("Pick a version to see what is inside it."), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_files = new QListWidget(this);
    m_files->setFrameShape(QFrame::NoFrame);
    m_files->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(250, 247, 240, 0.66); border: 1px solid rgba(201, 194, 180, 0.9);"
        " border-radius: 6px; padding: 6px; outline: none; }"
        "QListWidget::item { padding: 7px 10px; border-radius: 4px; color: #242e4c; outline: none; }"
        "QListWidget::item:selected { background: rgba(139, 36, 58, 0.12); }"));
    layout->addWidget(m_files, 1);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    m_toggle = new Button(tr("Turn off"), this);
    m_toggle->setPrimary(true);
    m_remove = new Button(tr("Delete"), this);
    auto* folder = new Button(tr("Open folder"), this);
    // Only a pack can be installed short of a file, and the button exists for exactly that state:
    // hidden while there is nothing owed, so it does not read as one more thing that does nothing.
    m_manual = new Button(tr("Fetch by hand"), this);
    m_manual->setToolTip(tr("Files this pack needs but their authors allow nobody to download"));
    m_manual->hide();
    actions->addWidget(m_toggle);
    actions->addWidget(m_remove);
    actions->addWidget(folder);
    actions->addWidget(m_manual);
    actions->addStretch(1);
    layout->addLayout(actions);

    connect(refresh, &QPushButton::clicked, this, &ContentPage::loadFiles);
    connect(m_version, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { loadFiles(); });
    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { loadFiles(); });
    connect(m_files, &QListWidget::itemSelectionChanged, this, &ContentPage::updateActions);
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        const auto* item = m_files->currentItem();
        if (!item || !kKinds[m_kind->currentIndex()].toggleable)
            return;
        // loadFiles() throws the items away, so the path and the name have to be copied now.
        const QString path = item->data(Qt::UserRole).toString();
        const QString name = item->text();
        const bool off = path.endsWith(disabledSuffix());
        const QString target = off ? path.left(path.size() - disabledSuffix().size()) : path + disabledSuffix();
        if (QFileInfo::exists(target))
            QFile::remove(target);
        QFile::rename(path, target);
        loadFiles();
        emit status(off ? tr("Turned on %1").arg(name) : tr("Turned off %1").arg(name));
    });
    connect(m_remove, &QPushButton::clicked, this, &ContentPage::removeSelected);
    connect(folder, &QPushButton::clicked, this, &ContentPage::openFolder);
    connect(m_manual, &QPushButton::clicked, this, &ContentPage::fetchByHand);

    reload();
}

void ContentPage::reload()
{
    const QString previous = m_version->currentData().toString();
    m_version->clear();

    const VersionStore store(versionsRoot());
    for (const auto& version : store.scan())
        m_version->addItem(version.id, version.id);

    if (m_version->count() == 0) {
        m_hint->setText(tr("No versions installed yet."));
        m_files->clear();
        m_manual->hide();
        updateActions();
        return;
    }

    const int index = m_version->findData(previous);
    m_version->setCurrentIndex(index >= 0 ? index : 0);
    loadFiles();
}

void ContentPage::selectVersion(const QString& versionId)
{
    const int index = m_version->findData(versionId);
    if (index >= 0) {
        m_version->setCurrentIndex(index);
        loadFiles();
    }
}

void ContentPage::loadFiles()
{
    const QString version = m_version->currentData().toString();
    // Hokora is swept before the folder is read: a file the player has just dropped in is in the mods
    // folder by the time the list is drawn, and refresh -- the button a person pokes after copying a
    // file somewhere -- is the moment they expect that to have happened.
    if (!version.isEmpty() && takeFromHokora())
        emit status(tr("Moved the files in hokora into this version"));

    m_files->clear();
    // The list is empty again, so both buttons are off until the player picks an item.
    updateActions();

    if (version.isEmpty())
        return;

    const QList<ManualMod> pending = readManualMods(versionDirectory(version));
    m_manual->setVisible(!pending.isEmpty());
    m_manual->setText(tr("Fetch by hand (%1)").arg(pending.size()));

    const QDir directory(QDir(versionDirectory(version)).filePath(m_kind->currentData().toString()));
    if (!directory.exists()) {
        m_hint->setText(tr("This version has no %1 folder yet.").arg(m_kind->currentText().toLower()));
        emit status(tr("Empty"));
        return;
    }

    const auto entries = directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& entry : entries) {
        if (entry.fileName().endsWith(QLatin1String(".disabled")) && entry.fileName().startsWith(QLatin1Char('.')))
            continue;
        const bool off = entry.fileName().endsWith(disabledSuffix());
        const QString label = off ? tr("%1 (off)").arg(entry.fileName().chopped(disabledSuffix().size())) : entry.fileName();
        auto* item = new QListWidgetItem(entry.isDir() ? label : QStringLiteral("%1 · %2 KB").arg(label).arg(entry.size() / 1024));
        item->setData(Qt::UserRole, entry.absoluteFilePath());
        m_files->addItem(item);
    }

    const int count = m_files->count();
    m_hint->setText(count == 0 ? tr("Nothing here yet.") : tr("%1 item(s) in %2.").arg(count).arg(directory.absolutePath()));
    emit status(tr("%1 item(s)").arg(count));
}

void ContentPage::updateActions()
{
    const auto* item = m_files->currentItem();
    const bool toggleable = kKinds[m_kind->currentIndex()].toggleable;
    const QString path = item ? item->data(Qt::UserRole).toString() : QString();
    m_remove->setEnabled(item != nullptr);
    m_toggle->setEnabled(item != nullptr && toggleable);
    m_toggle->setText(path.endsWith(disabledSuffix()) ? tr("Turn on") : tr("Turn off"));
}

void ContentPage::setEnabled(bool enabled)
{
    Q_UNUSED(enabled);
}

void ContentPage::removeSelected()
{
    const auto* item = m_files->currentItem();
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    const QFileInfo info(path);
    if (QMessageBox::question(this, tr("Delete"), tr("Delete %1? This cannot be undone.").arg(info.fileName()))
        != QMessageBox::Yes)
        return;

    const bool ok = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    loadFiles();
    emit status(ok ? tr("Deleted %1").arg(info.fileName()) : tr("Could not delete %1").arg(info.fileName()));
}

void ContentPage::openFolder()
{
    const QString version = m_version->currentData().toString();
    if (version.isEmpty())
        return;
    const QString path = QDir(versionDirectory(version)).filePath(m_kind->currentData().toString());
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool ContentPage::takeFromHokora()
{
    const QString version = m_version->currentData().toString();
    if (version.isEmpty())
        return false;
    return harvestHokora(versionDirectory(version)).taken > 0;
}

void ContentPage::fetchByHand()
{
    const QString version = m_version->currentData().toString();
    if (version.isEmpty())
        return;

    auto* dialog = new ManualModsDialog(version, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // The dialog moves files in as they arrive, so the folder behind it has to be redrawn, not just
    // when it closes: a player who fills the list wants to see it happen.
    connect(dialog, &ManualModsDialog::changed, this, [this] {
        loadFiles();
        emit status(tr("Moved the files in hokora into this version"));
    });
    dialog->show();
}
