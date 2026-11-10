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

/**
 * Where the data directory is when nobody said otherwise: `%APPDATA%\KomorebiLauncherLite`.
 *
 * This one never moves, so it is where the pointer to a moved data directory lives. Settings,
 * accounts and the game all live in `dataRoot()`, which cannot be read from a file inside itself.
 */
QString defaultDataRoot();

/// The file that remembers a data directory the player picked: `defaultDataRoot()/data-root.txt`.
QString dataRootPointer();

/**
 * Points the launcher at `directory` (an empty string goes back to `defaultDataRoot()`), and
 * returns false with `error` filled when the pointer could not be written.
 *
 * Nothing is moved here: the caller decides what to do with the files already on disk.
 */
bool setDataRoot(const QString& directory, QString* error);

/// Whether the data directory is one the player chose rather than the default.
bool hasChosenDataRoot();

/// Whether this start takes the data directory from `KOMOREBI_DATA`, which wins over any setting.
bool dataRootFromEnvironment();

/**
 * Whether the remembered data directory is no longer there.
 *
 * A pointer to a path that has gone — a folder moved by hand, one renamed, a drive not plugged in —
 * is otherwise indistinguishable from a first start: the launcher comes up with no versions, no
 * instances and no accounts, and never says why.
 */
bool chosenDataRootIsMissing();

/// Whether `directory` holds anything this launcher writes: settings, accounts, a version, assets…
bool looksLikeDataRoot(const QString& directory);

/// `<dataRoot>/instances`
QString instancesRoot();

/// `<dataRoot>/libraries`
QString librariesRoot();

/// `<dataRoot>/assets`
QString assetsRoot();

/// `<dataRoot>/versions`
QString versionsRoot();

/// `<dataRoot>/versions/<id>`
QString versionDirectory(const QString& versionId);

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
