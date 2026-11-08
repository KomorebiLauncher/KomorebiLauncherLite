// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Unpacking the archives the game ships inside other archives.
 *
 *  Older versions need their native libraries as loose files next to the game, and some loaders
 *  hand the launcher a zip inside a zip. Windows has had a tar since 2018 that reads zip, which is
 *  one fewer dependency than carrying an inflater -- and this launcher is Windows-only by design.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Komorebi {

/**
 * Extracts `archive` into `targetDirectory`.
 *
 * `only` names individual entries when just a few are wanted; empty means everything. Returns false
 * and sets `error` when the archive cannot be read or nothing could be written.
 */
bool extractArchive(const QString& archive, const QString& targetDirectory, QString* error, const QStringList& only = {});

/// Whether a file looks like a zip; used to decide whether extracting it is worth trying.
bool looksLikeArchive(const QString& path);

}  // namespace Komorebi
