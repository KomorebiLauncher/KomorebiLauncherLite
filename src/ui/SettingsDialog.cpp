// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SettingsDialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include "kit/Kit.h"

using namespace Komorebi;

SettingsDialog::SettingsDialog(const Settings& settings, QWidget* parent)
    : QDialog(parent), m_result(settings)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Settings"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    // language
    auto* languageRow = new QHBoxLayout();
    languageRow->addWidget(new QLabel(tr("Language"), this));
    m_language = new Komorebi::Combo(this);
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
    languageRow->addWidget(m_language, 1);
    layout->addLayout(languageRow);

    // memory
    auto* memoryRow = new QHBoxLayout();
    memoryRow->addWidget(new QLabel(tr("Memory (MB)"), this));
    m_memory = new Komorebi::Field(this);
    m_memory->setValidator(new QIntValidator(512, 65536, m_memory));
    m_memory->setText(QString::number(settings.memoryMb));
    memoryRow->addWidget(m_memory, 1);
    layout->addLayout(memoryRow);

    // java
    auto* javaRow = new QHBoxLayout();
    javaRow->addWidget(new QLabel(tr("Java path"), this));
    m_java = new Komorebi::Field(this);
    m_java->setPlaceholderText(tr("Empty: choose the runtime the version asks for"));
    m_java->setText(settings.javaPath);
    javaRow->addWidget(m_java, 1);
    layout->addLayout(javaRow);

    auto* hint = new QLabel(tr("A language change takes effect the next time the launcher starts."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
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
        m_result.language = m_language->currentData().toString();
        m_result.memoryMb = m_memory->text().toInt();
        if (m_result.memoryMb < 512)
            m_result.memoryMb = 512;
        m_result.javaPath = m_java->text().trimmed();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}
