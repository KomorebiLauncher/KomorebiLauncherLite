// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AppWindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "core/Instance.h"
#include "kit/Kit.h"

using Komorebi::Instance;

class InstanceModel final : public QAbstractListModel {
   public:
    using QAbstractListModel::QAbstractListModel;

    void setInstances(QList<Instance> instances)
    {
        beginResetModel();
        m_instances = std::move(instances);
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_instances.size()); }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_instances.size())
            return {};
        const auto& instance = m_instances.at(index.row());
        if (role == Qt::DisplayRole)
            return instance.name;
        return {};
    }

    const Instance& at(int row) const { return m_instances.at(row); }

   private:
    QList<Instance> m_instances;
};

class CardDelegate final : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return QSize(option.rect.width(), 76);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const auto* model = dynamic_cast<const InstanceModel*>(index.model());
        if (!model)
            return;
        const Instance& instance = model->at(index.row());
        const Komorebi::Colours colours = Komorebi::Colours::current();

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect card = option.rect.adjusted(6, 4, -6, -4);
        QPainterPath path;
        path.addRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), Komorebi::Radius::Surface, Komorebi::Radius::Surface);
        painter->fillPath(path, selected ? colours.wash(26)
                                         : QColor(colours.paper.red(), colours.paper.green(), colours.paper.blue(),
                                                  hovered ? 215 : 190));
        painter->setPen(QPen(selected ? colours.wash(130) : colours.line, 1.0));
        painter->drawPath(path);

        // A crest mark stands in for the icon until the icon set is wired in.
        const QRect mark(card.left() + 16, card.center().y() - 18, 36, 36);
        painter->setPen(QPen(colours.plum, 1.2));
        painter->drawEllipse(mark);
        painter->drawEllipse(mark.adjusted(10, 10, -10, -10));
        painter->setFont(option.font);
        painter->drawText(mark, Qt::AlignCenter, QStringLiteral("木"));

        QFont title = option.font;
        title.setBold(true);
        painter->setFont(title);
        painter->setPen(instance.playable ? colours.ink : colours.inkSoft);
        painter->drawText(QRect(mark.right() + 16, card.top() + 14, card.width() - 90, 20), Qt::AlignLeft | Qt::AlignVCenter,
                          instance.name);

        painter->setFont(option.font);
        painter->setPen(colours.inkSoft);
        painter->drawText(QRect(mark.right() + 16, card.top() + 38, card.width() - 90, 18), Qt::AlignLeft | Qt::AlignVCenter,
                          instance.summary());

        painter->restore();
    }
};

AppWindow::AppWindow(Komorebi::Theme theme, QString instancesRoot, QWidget* parent)
    : QMainWindow(parent), m_theme(std::move(theme)), m_instancesRoot(std::move(instancesRoot))
{
    setWindowTitle(QStringLiteral("木漏れ日 Launcher"));
    resize(1100, 720);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // top bar
    auto* bar = new QWidget(central);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(16, 10, 16, 10);
    barLayout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("木漏れ日"), bar);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    barLayout->addWidget(title);
    barLayout->addStretch(1);

    auto* add = new Komorebi::Button(tr("新建实例"), bar);
    add->setPrimary(true);
    auto* settings = new Komorebi::Button(tr("设置"), bar);
    auto* folders = new Komorebi::Button(tr("文件夹"), bar);
    barLayout->addWidget(add);
    barLayout->addWidget(folders);
    barLayout->addWidget(settings);
    layout->addWidget(bar);

    // instance list
    m_view = new QListView(central);
    m_view->setModel(m_model = new InstanceModel(m_view));
    m_view->setItemDelegate(new CardDelegate(m_view));
    m_view->setMouseTracking(true);
    m_view->setUniformItemSizes(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setStyleSheet(QStringLiteral("QListView { background: transparent; }"));
    layout->addWidget(m_view, 1);

    m_empty = new QLabel(tr("这里还没有实例。"), central);
    m_empty->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_empty);

    setCentralWidget(central);
    reload();
}

void AppWindow::reload()
{
    const Komorebi::InstanceStore store(m_instancesRoot);
    const auto instances = store.scan();
    m_model->setInstances(instances);
    m_view->setVisible(!instances.isEmpty());
    m_empty->setVisible(instances.isEmpty());
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
