// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Things the launcher has to do to Windows, not to the game.
 *
 *  Windows keeps a per-program graphics preference, remembers it, and honours it the next time that
 *  program starts; the only way to set it is the registry. Process priority is the same kind of
 *  thing: it belongs to the running process, not to its command line.
 */

#pragma once

#include <QString>

namespace Komorebi {

/**
 * Tells Windows which GPU a program should use.
 *
 * `high` selects the discrete card, false hands the choice back to Windows. The setting is per
 * executable and survives reboots, which is why it is worth writing once instead of per launch.
 */
bool setGpuPreference(const QString& executable, bool high);

/// Sets the priority class of a running process; the level is "high", "above", "below" or empty.
bool setProcessPriority(qint64 processId, const QString& level);

}  // namespace Komorebi
