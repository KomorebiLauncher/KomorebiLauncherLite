// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Where the launcher keeps its things.
 *
 *  There is one data directory and it is the folder the launcher was started from, so the whole
 *  installation is a folder a player can copy, back up or move between drives without anything
 *  having to be told where it went. `KOMOREBI_DATA` is the only way to point a copy somewhere else,
 *  and it exists for the test harness rather than for players.
 */

#pragma once

#include <QString>

namespace Komorebi {

/// The launcher's data directory, with a trailing separator removed.
QString dataRoot();

/// Where the data directory is: the folder the executable sits in.
QString defaultDataRoot();

/**
 * Whether the data directory can be written to, creating it first when it is missing.
 *
 * The launcher ships as a single file that is meant to be started from a folder of its own, so a
 * folder it cannot write to (Program Files, a read-only drive, inside an archive) is a reason to say
 * so and stop: without it settings, accounts and everything the game downloads cannot be kept.
 * Returns false with `error` filled when the directory cannot be created or written to.
 */
bool dataRootIsWritable(QString* error);

/// Whether this start takes the data directory from `KOMOREBI_DATA`, which wins over everything.
bool dataRootFromEnvironment();

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
