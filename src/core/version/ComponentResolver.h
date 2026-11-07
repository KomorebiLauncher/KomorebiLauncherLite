// SPDX-License-Identifier: GPL-3.0-only
/*
 *  An instance is not one version: it is a stack of components -- the game, the LWJGL natives, the
 *  loader -- and the launcher has to merge them into the single description the game expects.
 *
 *  The order is the one the components themselves declare: net.minecraft is -2, org.lwjgl3 is -1,
 *  a loader sits above both. Later components win where they overlap, and the loader's main class
 *  is the one that runs.
 */

#pragma once

#include <QList>
#include <QString>

#include "core/version/VersionJson.h"

namespace Komorebi {

struct ComponentRef {
    QString uid;
    QString version;
};

/// The components an instance is made of, read from its mmc-pack.json.
QList<ComponentRef> readComponentList(const QString& instanceDirectory);

/// Merges the components found under `metaRoot/<uid>/<version>.json` into one version description.
VersionJson mergeComponents(const QString& metaRoot, const QList<ComponentRef>& components);

/// Reads a component file; empty when it is not on disk.
VersionJson readComponent(const QString& metaRoot, const ComponentRef& component);

/// Whether every component of the instance is present in the meta directory.
QStringList missingComponents(const QString& metaRoot, const QList<ComponentRef>& components);

}  // namespace Komorebi
