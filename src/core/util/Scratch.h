// SPDX-License-Identifier: GPL-3.0-only
/*
 *  A scratch directory inside the launcher's own data directory.
 *
 *  The system temp directory is not something a launcher may rely on: it can be locked by policy,
 *  owned by another account, full, or simply missing, and the installers that write tens of megabytes
 *  beside the game then fail with a message that explains nothing. A directory the launcher creates
 *  under its own data root is one it has already proven it can write to.
 */

#pragma once

#include <QString>

namespace Komorebi {

class ScratchDirectory {
   public:
    /// Creates `<data>/tmp/<tag>-<random>`; `isValid()` says whether it could.
    explicit ScratchDirectory(const QString& tag);
    ~ScratchDirectory();

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    QString path() const { return m_path; }
    bool isValid() const { return m_valid; }

   private:
    QString m_path;
    bool m_valid = false;
};

}  // namespace Komorebi
