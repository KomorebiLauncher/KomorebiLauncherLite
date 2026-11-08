// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Throwing away what the game leaves behind. Logs and crash reports are useful for a day and then
 *  they are just files.
 */

#pragma once

#include <QString>

namespace Komorebi {

/// Deletes logs, crash reports and temporary files under `dataRoot`. Returns the bytes freed.
qint64 cleanJunk(const QString& dataRoot);

}  // namespace Komorebi
