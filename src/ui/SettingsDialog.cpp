// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

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
    m_width->setPlaceholderText(tr("width"));
    m_height->setPlaceholderText(tr("height"));
    sizeRow->addWidget(m_width);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), page));
    sizeRow->addWidget(m_height);
    sizeRow->addStretch(1);
    launchForm->addRow(tr("Window size"), sizeRow);

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
    m_downloadSite->addItem(tr("Official servers"), QStringLiteral("official"));
    m_downloadSite->addItem(tr("Mirror (BG)"), QStringLiteral("mirror"));
    for (int i = 0; i < m_downloadSite->count(); ++i) {
        if (m_downloadSite->itemData(i).toString() == settings.downloadSite) {
            m_downloadSite->setCurrentIndex(i);
            break;
        }
    }
    downloadForm->addRow(tr("Download from"), m_downloadSite);

    m_concurrency = numberField(page, 1, 64, settings.maxConcurrency);
    downloadForm->addRow(tr("Files at once"), m_concurrency);

    m_speedLimit = numberField(page, 0, 1048576, settings.speedLimitKb);
    m_speedLimit->setPlaceholderText(tr("0: no limit"));
    downloadForm->addRow(tr("Speed limit (KB/s)"), m_speedLimit);
    layout->addLayout(downloadForm);

    // ---- appearance
    auto* appearForm = new QFormLayout();
    appearForm->setSpacing(8);
    m_wallpaperOpacity = new Komorebi::Slider(Qt::Horizontal, page);
    m_wallpaperOpacity->setRange(0, 100);
    m_wallpaperOpacity->setValue(settings.wallpaperOpacity);
    appearForm->addRow(tr("Wallpaper"), m_wallpaperOpacity);
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
        m_result.maxConcurrency = qMax(1, m_concurrency->text().toInt());
        m_result.speedLimitKb = qMax(0, m_speedLimit->text().toInt());
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}
