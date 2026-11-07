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

}  // namespace Komorebi
