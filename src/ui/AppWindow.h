// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>

#include "theme/Theme.h"

class QListView;
class QLabel;

class AppWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit AppWindow(Komorebi::Theme theme, QString instancesRoot, QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    void reload();

    Komorebi::Theme m_theme;
    QString m_instancesRoot;
    QListView* m_view = nullptr;
    QLabel* m_empty = nullptr;
    class InstanceModel* m_model = nullptr;
};
