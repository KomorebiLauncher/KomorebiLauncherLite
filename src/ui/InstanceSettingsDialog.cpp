// SPDX-License-Identifier: GPL-3.0-only
#include "ui/InstanceSettingsDialog.h"

#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include "kit/Kit.h"

using namespace Komorebi;

InstanceSettingsDialog::InstanceSettingsDialog(const QString& versionId, const InstanceConfig& config,
                                               int launcherMemoryMb, QWidget* parent)
    : QDialog(parent), m_versionId(versionId), m_result(config)
{
    setWindowTitle(tr("Settings for %1").arg(versionId));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("Settings for %1").arg(versionId), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    m_name = new Komorebi::Field(this);
    m_name->setPlaceholderText(tr("Empty: the version id"));
    m_name->setText(config.displayName);
    form->addRow(tr("Name"), m_name);

    m_memory = new Komorebi::Field(this);
    m_memory->setValidator(new QIntValidator(0, 65536, m_memory));
    m_memory->setPlaceholderText(tr("Empty or 0: follow the launcher (%1 MB)").arg(launcherMemoryMb));
    m_memory->setText(config.memoryMb > 0 ? QString::number(config.memoryMb) : QString());
    form->addRow(tr("Memory (MB)"), m_memory);

    m_jvmArguments = new Komorebi::Field(this);
    m_jvmArguments->setPlaceholderText(tr("Empty: follow the launcher"));
    m_jvmArguments->setText(config.jvmArguments);
    form->addRow(tr("JVM arguments"), m_jvmArguments);

    m_gameArguments = new Komorebi::Field(this);
    m_gameArguments->setPlaceholderText(tr("Empty: follow the launcher"));
    m_gameArguments->setText(config.gameArguments);
    form->addRow(tr("Game arguments"), m_gameArguments);

    m_description = new Komorebi::Field(this);
    m_description->setPlaceholderText(tr("Notes shown on this version's page"));
    m_description->setText(config.description);
    form->addRow(tr("Notes"), m_description);

    layout->addLayout(form);
    layout->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* save = new Komorebi::Button(tr("Save"), this);
    save->setPrimary(true);
    auto* cancel = new Komorebi::Button(tr("Cancel"), this);
    buttons->addWidget(save);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(save, &QPushButton::clicked, this, [this] {
        m_result.displayName = m_name->text().trimmed();
        m_result.memoryMb = m_memory->text().toInt();
        m_result.jvmArguments = m_jvmArguments->text().trimmed();
        m_result.gameArguments = m_gameArguments->text().trimmed();
        m_result.description = m_description->text().trimmed();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}
