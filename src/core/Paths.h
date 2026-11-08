// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Where the launcher keeps its things.
 *
 *  While the rewrite is young it shares the directory the installed launcher already uses, so the
 *  same instances, libraries and assets can be read by both. Qt's "generic config" is Local on
 *  Windows, which is not where the launcher put anything, so the roaming directory is asked for
 *  directly.
 */

#pragma once

#include <QString>

namespace Komorebi {

/// The launcher's data directory, with a trailing separator removed.
QString dataRoot();

/// `<dataRoot>/instances`
QString instancesRoot();

/// `<dataRoot>/libraries`
QString librariesRoot();

/// `<dataRoot>/assets`
QString assetsRoot();

/**
 * Joins `relative` onto `root`, or returns an empty string when the result would leave it.
 *
 * Everything the launcher writes or reads by a path built from remote metadata goes through here:
 * a version id, an asset index id or a component name comes from a server, and "..", an absolute
 * path or a drive letter in one of them must not decide where a file lands.
 */
QString safeJoin(const QString& root, const QString& relative);

/// Whether `target`, once normalised, is still inside `root`.
bool staysInside(const QString& root, const QString& target);

/// A single path component that is safe to use: no separators, no "..", not empty.
bool isSafeComponent(const QString& value);

}  // namespace Komorebi
