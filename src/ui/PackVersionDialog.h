// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Which file of a pack to install.
 *
 *  A pack has many releases and the newest one is often the beta of the next: installing a pack by
 *  its newest file is how a player ends up on a version that breaks, without ever being told what
 *  they were getting. The list says what each file is -- its version, whether it is a release, when
 *  it was published and how big it is -- and asks the source for the next page as the player scrolls
 *  down, so the first screen costs one request and the rest is optional.
 *
 *  The page is fetched on a thread of its own: the dialog is up as soon as it opens, and a slow
 *  source shows as a line of text rather than as a window that never appears.
 */

#pragma once

#include <QDialog>
#include <QList>

#include <functional>
#include <memory>

#include "core/content/CurseForge.h"

class QLabel;
class QTableWidget;
class QTimer;

namespace Komorebi {
class Button;
}

class PackVersionDialog : public QDialog {
    Q_OBJECT

   public:
    /// Fetches one page: `offset` files are already known, at most `limit` more are wanted, and
    /// `total` is set to how many the source has in all when it says so.
    using Fetcher =
        std::function<QList<Komorebi::CurseForgeFile>(int offset, int limit, int* total, QString* error)>;

    /// `fetcher` is copied and called on a thread that outlives neither it nor the dialog.
    PackVersionDialog(const QString& packName, Fetcher fetcher, QWidget* parent = nullptr);

    /// The file to install, meaningfully filled in only after exec() returned Accepted.
    Komorebi::CurseForgeFile chosen() const { return m_chosen; }

    /// The file the list is currently on, before anything is accepted.
    Komorebi::CurseForgeFile selected() const;

    /// How many files the list holds; the self-test waits on this instead of on the network.
    int fileCount() const { return m_files.size(); }

   private:
    struct Page;

    void fetch(int offset);
    void applyPage();
    void selectDefault();
    void updateButtons();
    void accept() override;

    Fetcher m_fetcher;
    QList<Komorebi::CurseForgeFile> m_files;
    Komorebi::CurseForgeFile m_chosen;
    std::shared_ptr<Page> m_page;
    int m_total = 0;
    bool m_loading = false;
    bool m_exhausted = false;

    QLabel* m_hint = nullptr;
    QTableWidget* m_table = nullptr;
    Komorebi::Button* m_more = nullptr;
    Komorebi::Button* m_ok = nullptr;
    QTimer* m_poll = nullptr;
};
