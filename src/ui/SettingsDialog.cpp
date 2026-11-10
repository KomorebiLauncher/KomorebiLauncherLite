// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/Paths.h"
#include "core/util/DataMove.h"
#include "kit/Kit.h"

using namespace Komorebi;

namespace {

QLineEdit* numberField(QWidget* parent, int minimum, int maximum, int value)
{
    auto* field = new Komorebi::Field(parent);
    field->setValidator(new QIntValidator(minimum, maximum, field));
    field->setText(QString::number(value));
    return field;
}

/// A sentence under a field: softer ink than the field itself, and a bounded width so it wraps
/// instead of stretching the dialog.
QLabel* hintLabel(QWidget* parent, const QString& text)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setMaximumWidth(400);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(Komorebi::Colours::current().inkSoft.name()));
    return label;
}

}  // namespace

SettingsDialog::SettingsDialog(const Settings& settings, QWidget* parent)
    : QDialog(parent), m_result(settings)
{
    setWindowTitle(tr("Settings"));
    resize(560, 640);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);

    auto* page = new QWidget(scroll);
    scroll->setWidget(page);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 12);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Settings"), page);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    // ---- where the game lives
    // Asked for first because it is the one setting that can free a whole drive: versions, assets,
    // libraries, Java and the saves all sit under it.
    auto* dataForm = new QFormLayout();
    dataForm->setSpacing(8);
    m_dataDirectory = QDir::cleanPath(Komorebi::dataRoot());
    m_dataDirectoryField = new Komorebi::Field(page);
    m_dataDirectoryField->setReadOnly(true);
    auto* browseData = new Komorebi::Button(tr("Browse…"), page);
    auto* defaultData = new Komorebi::Button(tr("Default"), page);
    // A path is long and a read-only field scrolls to its tail, so the interesting part would be the
    // end of the path at every moment; put the caret back where it started after each change.
    const auto showDataDirectory = [this] {
        const QString shown = QDir::toNativeSeparators(m_dataDirectory);
        m_dataDirectoryField->setText(shown);
        m_dataDirectoryField->setCursorPosition(0);
        m_dataDirectoryField->setToolTip(shown);
    };
    showDataDirectory();
    auto* dataRow = new QVBoxLayout();
    dataRow->setSpacing(6);
    dataRow->addWidget(m_dataDirectoryField);
    auto* dataButtons = new QHBoxLayout();
    dataButtons->addStretch(1);
    dataButtons->addWidget(browseData);
    dataButtons->addWidget(defaultData);
    dataRow->addLayout(dataButtons);
    dataForm->addRow(tr("Data directory"), dataRow);
    dataForm->addRow(
        QString(),
        hintLabel(page, tr("Versions, libraries, assets, Java, accounts and your saves all live here. "
                           "Moving it to another drive is fine; the launcher asks before copying anything, "
                           "and the change takes effect the next time it starts.")));
    layout->addLayout(dataForm);

    connect(browseData, &QPushButton::clicked, this, [this, showDataDirectory] {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Choose a data directory"), m_dataDirectory, QFileDialog::ShowDirsOnly);
        if (chosen.isEmpty())
            return;
        m_dataDirectory = QDir::cleanPath(QDir::fromNativeSeparators(chosen));
        showDataDirectory();
    });
    connect(defaultData, &QPushButton::clicked, this, [this, showDataDirectory] {
        m_dataDirectory = QDir::cleanPath(Komorebi::defaultDataRoot());
        showDataDirectory();
    });

    // ---- launching
    auto* launchForm = new QFormLayout();
    launchForm->setSpacing(8);

    m_language = new Komorebi::Combo(page);
    m_language->addItem(tr("Follow the system"), QStringLiteral("auto"));
    m_language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
    for (int i = 0; i < m_language->count(); ++i) {
        if (m_language->itemData(i).toString() == settings.language) {
            m_language->setCurrentIndex(i);
            break;
        }
    }
    launchForm->addRow(tr("Language"), m_language);

    m_memory = numberField(page, 512, 65536, settings.memoryMb);
    launchForm->addRow(tr("Memory (MB)"), m_memory);

    m_lockMemory = new QCheckBox(tr("Lock the heap (-Xms = -Xmx)"), page);
    m_lockMemory->setChecked(settings.lockMemory);
    launchForm->addRow(QString(), m_lockMemory);

    auto* sizeRow = new QHBoxLayout();
    m_width = numberField(page, 0, 16384, settings.width);
    m_height = numberField(page, 0, 16384, settings.height);
    // 0 means "whatever the version asks for": an empty field lets the placeholder say so instead
    // of showing a literal 0.
    if (settings.width == 0)
        m_width->clear();
    if (settings.height == 0)
        m_height->clear();
    m_width->setPlaceholderText(tr("width"));
    m_height->setPlaceholderText(tr("height"));
    sizeRow->addWidget(m_width);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), page));
    sizeRow->addWidget(m_height);
    sizeRow->addStretch(1);
    launchForm->addRow(tr("Game window size"), sizeRow);
    launchForm->addRow(QString(), hintLabel(page, tr("0 means whatever the version asks for; an empty field means the same.")));

    m_java = new Komorebi::Field(page);
    m_java->setPlaceholderText(tr("Empty: choose the runtime the version asks for"));
    m_java->setText(settings.javaPath);
    launchForm->addRow(tr("Java path"), m_java);

    m_jvmArguments = new Komorebi::Field(page);
    m_jvmArguments->setPlaceholderText(QStringLiteral("-XX:+UseG1GC …"));
    m_jvmArguments->setText(settings.jvmArguments);
    launchForm->addRow(tr("JVM arguments"), m_jvmArguments);

    m_gameArguments = new Komorebi::Field(page);
    m_gameArguments->setPlaceholderText(QStringLiteral("--fullscreen …"));
    m_gameArguments->setText(settings.gameArguments);
    launchForm->addRow(tr("Game arguments"), m_gameArguments);

    m_preLaunch = new Komorebi::Field(page);
    m_preLaunch->setPlaceholderText(tr("Optional command to run before the game"));
    m_preLaunch->setText(settings.preLaunchCommand);
    launchForm->addRow(tr("Before launch"), m_preLaunch);

    m_priority = new Komorebi::Combo(page);
    m_priority->addItem(tr("Normal"), QString());
    m_priority->addItem(tr("Above normal"), QStringLiteral("above"));
    m_priority->addItem(tr("High"), QStringLiteral("high"));
    m_priority->addItem(tr("Below normal"), QStringLiteral("below"));
    for (int i = 0; i < m_priority->count(); ++i) {
        if (m_priority->itemData(i).toString() == settings.processPriority) {
            m_priority->setCurrentIndex(i);
            break;
        }
    }
    launchForm->addRow(tr("Process priority"), m_priority);

    m_highPerformanceGpu = new QCheckBox(tr("Ask for the high performance GPU"), page);
    m_highPerformanceGpu->setChecked(settings.highPerformanceGpu);
    launchForm->addRow(QString(), m_highPerformanceGpu);

    m_legacyFix = new QCheckBox(tr("Legacy fix (old versions)"), page);
    m_legacyFix->setChecked(settings.legacyFix);
    launchForm->addRow(QString(), m_legacyFix);

    m_unsafeAgent = new QCheckBox(tr("Allow the unsafe memory access agent"), page);
    m_unsafeAgent->setChecked(settings.unsafeAgent);
    launchForm->addRow(QString(), m_unsafeAgent);

    layout->addLayout(launchForm);

    // ---- downloads
    auto* downloadForm = new QFormLayout();
    downloadForm->setSpacing(8);
    m_downloadSite = new Komorebi::Combo(page);
    m_downloadSite->addItem(tr("Official first"), QStringLiteral("official-first"));
    m_downloadSite->addItem(tr("Mirror first"), QStringLiteral("mirror-first"));
    m_downloadSite->addItem(tr("Official only"), QStringLiteral("official-only"));
    m_downloadSite->addItem(tr("Mirror only"), QStringLiteral("mirror-only"));
    for (int i = 0; i < m_downloadSite->count(); ++i) {
        if (m_downloadSite->itemData(i).toString() == settings.downloadSite) {
            m_downloadSite->setCurrentIndex(i);
            break;
        }
    }
    downloadForm->addRow(tr("Download from"), m_downloadSite);

    m_mirrorBase = new Komorebi::Field(page);
    m_mirrorBase->setPlaceholderText(QStringLiteral("https://bmclapi2.bangbang93.com"));
    m_mirrorBase->setText(settings.mirrorBase);
    downloadForm->addRow(tr("Mirror address"), m_mirrorBase);

    m_concurrency = numberField(page, 1, 64, settings.maxConcurrency);
    downloadForm->addRow(tr("Files at once"), m_concurrency);

    m_speedLimit = numberField(page, 0, 1048576, settings.speedLimitKb);
    m_speedLimit->setPlaceholderText(tr("0: no limit"));
    downloadForm->addRow(tr("Speed limit (KB/s)"), m_speedLimit);
    layout->addLayout(downloadForm);

    // ---- signing in
    auto* accountForm = new QFormLayout();
    accountForm->setSpacing(8);
    m_msaClientId = new Komorebi::Field(page);
    m_msaClientId->setPlaceholderText(QStringLiteral("00000000-0000-0000-0000-000000000000"));
    m_msaClientId->setText(settings.msaClientId);
    accountForm->addRow(tr("Microsoft app id (sign-in)"), m_msaClientId);
    accountForm->addRow(
        QString(),
        hintLabel(page, tr("Only needed to sign in with a Microsoft account: create a public-client app in "
                           "Azure and paste its application (client) id here. Playing offline needs nothing "
                           "in this field, and the launcher ships no app id of its own, so leaving it empty "
                           "means Microsoft sign-in can only fail. The KOMOREBI_MSA_CLIENT_ID environment "
                           "variable also works.")));
    layout->addLayout(accountForm);

    // ---- appearance
    auto* appearForm = new QFormLayout();
    appearForm->setSpacing(8);
    auto* wallpaperRow = new QHBoxLayout();
    m_wallpaperOpacity = new Komorebi::Slider(Qt::Horizontal, page);
    m_wallpaperOpacity->setRange(0, 100);
    m_wallpaperOpacity->setValue(settings.wallpaperOpacity);
    auto* opacityValue = new QLabel(QStringLiteral("%1%").arg(m_wallpaperOpacity->value()), page);
    opacityValue->setMinimumWidth(40);
    opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_wallpaperOpacity, &QSlider::valueChanged, this, [opacityValue](int value) {
        opacityValue->setText(QStringLiteral("%1%").arg(value));
    });
    wallpaperRow->addWidget(m_wallpaperOpacity, 1);
    wallpaperRow->addWidget(opacityValue);
    appearForm->addRow(tr("Wallpaper"), wallpaperRow);
    appearForm->addRow(QString(), hintLabel(page, tr("Has no effect when the theme carries no wallpaper.")));
    layout->addLayout(appearForm);

    auto* hint = new QLabel(tr("A language change takes effect the next time the launcher starts."), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(20, 0, 20, 16);
    buttons->addStretch(1);
    auto* save = new Komorebi::Button(tr("Save"), this);
    save->setPrimary(true);
    auto* cancel = new Komorebi::Button(tr("Cancel"), this);
    buttons->addWidget(save);
    buttons->addWidget(cancel);
    outer->addLayout(buttons);

    connect(save, &QPushButton::clicked, this, [this] {
        m_result.language = m_language->currentData().toString();
        m_result.memoryMb = qMax(512, m_memory->text().toInt());
        m_result.lockMemory = m_lockMemory->isChecked();
        m_result.width = m_width->text().toInt();
        m_result.height = m_height->text().toInt();
        m_result.javaPath = m_java->text().trimmed();
        m_result.jvmArguments = m_jvmArguments->text().trimmed();
        m_result.gameArguments = m_gameArguments->text().trimmed();
        m_result.preLaunchCommand = m_preLaunch->text().trimmed();
        m_result.highPerformanceGpu = m_highPerformanceGpu->isChecked();
        m_result.legacyFix = m_legacyFix->isChecked();
        m_result.unsafeAgent = m_unsafeAgent->isChecked();
        m_result.wallpaperOpacity = m_wallpaperOpacity->value();
        m_result.processPriority = m_priority->currentData().toString();
        m_result.downloadSite = m_downloadSite->currentData().toString();
        m_result.mirrorBase = m_mirrorBase->text().trimmed();
        m_result.maxConcurrency = qMax(1, m_concurrency->text().toInt());
        m_result.speedLimitKb = qMax(0, m_speedLimit->text().toInt());
        m_result.msaClientId = m_msaClientId->text().trimmed();

        if (QDir::cleanPath(m_dataDirectory) != QDir::cleanPath(Komorebi::dataRoot())) {
            QString error;
            if (!switchDataDirectory(m_dataDirectory, &error)) {
                if (!error.isEmpty())
                    QMessageBox::warning(this, tr("Data directory"), error);
                return;
            }
        }
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

bool SettingsDialog::switchDataDirectory(const QString& target, QString* error)
{
    const QString current = QDir::cleanPath(Komorebi::dataRoot());
    const QString wanted = QDir::cleanPath(target);
    const auto show = [](const QString& path) { return QDir::toNativeSeparators(path); };

    if (!QDir::isAbsolutePath(wanted)) {
        *error = tr("%1 is not a full path.").arg(show(wanted));
        return false;
    }
    // Copying a directory onto itself, or into a directory that contains it, would walk in circles.
    if (Komorebi::staysInside(current, wanted) || Komorebi::staysInside(wanted, current)) {
        *error = tr("The new directory has to be outside the current one, and not contain it either.");
        return false;
    }

    // Copying is offered rather than assumed: someone pointing at an empty drive usually wants their
    // files along, someone pointing at a folder they already filled does not.
    QMessageBox ask(QMessageBox::Question, tr("Data directory"),
                    tr("Move everything from\n%1\nto\n%2?").arg(show(current), show(wanted)), QMessageBox::NoButton, this);
    auto* copyAndSwitch = ask.addButton(tr("Copy and switch"), QMessageBox::AcceptRole);
    auto* switchOnly = ask.addButton(tr("Switch without copying"), QMessageBox::DestructiveRole);
    auto* keepIt = ask.addButton(tr("Leave it alone"), QMessageBox::RejectRole);
    ask.setDefaultButton(copyAndSwitch);
    ask.exec();
    if (ask.clickedButton() == keepIt || ask.clickedButton() == nullptr)
        return false;

    if (ask.clickedButton() == copyAndSwitch) {
        if (!QDir().mkpath(wanted)) {
            *error = tr("Could not create %1.").arg(show(wanted));
            return false;
        }

        QProgressDialog progress(tr("Copying the data directory…"), tr("Stop"), 0, 100, this);
        progress.setWindowTitle(tr("Data directory"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);

        const Komorebi::DataCopyResult copy = Komorebi::copyDataDirectory(
            current, wanted, [&progress](const QString& path, int files, qint64 bytes) {
                // The number of files is not known until the walk is done, so the bar reports motion
                // and the label says what is happening.
                progress.setLabelText(tr("Copying %1\n%2 files, %3 MB")
                                          .arg(path)
                                          .arg(files)
                                          .arg(bytes / (1024.0 * 1024.0), 0, 'f', 1));
                progress.setValue(qMin(99, progress.value() + 1));
                QCoreApplication::processEvents();
                return !progress.wasCanceled();
            });
        progress.setValue(100);

        if (copy.cancelled) {
            *error = tr("Copying stopped, so the data directory is unchanged. Anything already copied is "
                        "left in %1 and can be deleted.")
                         .arg(show(wanted));
            return false;
        }
        if (!copy.error.isEmpty()) {
            *error = tr("Copying failed: %1\nThe data directory is unchanged.").arg(copy.error);
            return false;
        }
    }

    // Back to the default means "forget the pointer", not "remember the default as a choice".
    const QString remember = wanted == QDir::cleanPath(Komorebi::defaultDataRoot()) ? QString() : wanted;
    QString pointerError;
    if (!Komorebi::setDataRoot(remember, &pointerError)) {
        *error = tr("Could not remember the new data directory: %1").arg(pointerError);
        return false;
    }

    m_dataDirectoryChanged = true;
    error->clear();
    return true;
}
