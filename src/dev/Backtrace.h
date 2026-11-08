// SPDX-License-Identifier: GPL-3.0-only
/*
 *  When the launcher hits a fatal Qt check, the message alone does not say where it happened: the
 *  check fires deep inside Qt, and a debug build answers with a dialog that has to be clicked. This
 *  writes the message and the call stack, with file and line where the symbols have them, to
 *  crash.txt next to the executable -- a file that can be read after the process is gone.
 */

#pragma once

namespace Komorebi {

/// Replaces Qt's fatal handler with one that records the stack and ends the process.
void installFatalBacktrace();

}  // namespace Komorebi
