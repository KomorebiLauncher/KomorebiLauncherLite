// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Komorebi Launcher, written from zero.
 *
 *  The core knows about directories and files; the kit knows how a control looks; the window knows
 *  how they meet. Nothing here includes anything from the old launcher.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Komorebi {

enum class Loader {
    Vanilla,
    Forge,
    NeoForge,
    Fabric,
    Quilt,
    Unknown,
};

QString loaderName(Loader loader);

/// One instance as it exists on disk. Reading it is the core's job; nothing else parses these files.
struct Instance {
    QString directory;         ///< absolute path
    QString id;                ///< directory name, which is the identity the launcher uses
    QString name;
    QString iconKey;
    QString group;
    QString minecraftVersion;
    QString loaderVersion;
    Loader loader = Loader::Vanilla;
    bool playable = false;

    /// A one line summary for a card: "1.20.1 · Forge 47.2.0".
    QString summary() const;
};

/// Every instance under one root, in the order the directory listing gives them.
class InstanceStore {
   public:
    explicit InstanceStore(QString root);

    /// Re-reads the directory. Cheap enough to call on demand: it parses a few small files per
    /// instance and nothing else.
    QList<Instance> scan() const;

    QString root() const { return m_root; }

   private:
    QString m_root;
};

}  // namespace Komorebi
