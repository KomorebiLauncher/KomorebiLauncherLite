// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Loading a version that inherits from another one.
 *
 *  Every loader describes itself as a difference: "Fabric on top of 1.20.1" says what it adds, and
 *  points at the version it builds on. The game only understands a complete description, so the
 *  chain is walked and merged before anything is launched or downloaded.
 */

#pragma once

#include <QString>
#include <QStringList>

#include "core/version/VersionJson.h"

namespace Komorebi {

struct ResolvedVersion {
    VersionJson version;      ///< the whole description, inheritance flattened
    QStringList chain;        ///< the ids that were merged, child first
    QString baseId;           ///< the version at the bottom: it owns the game jar and the assets
    QString error;            ///< set when the chain could not be walked

    bool ok() const { return error.isEmpty() && !version.id.isEmpty(); }
};

/**
 * Reads `versions/<id>/<id>.json` and every `inheritsFrom` above it, and merges them.
 *
 * The child wins where the two disagree on identity (id, main class, arguments it brings); the
 * parent supplies what a loader does not repeat (the game jar, the asset index, the game's own
 * arguments).
 */
ResolvedVersion resolveVersion(const QString& versionsRoot, const QString& id);

}  // namespace Komorebi
