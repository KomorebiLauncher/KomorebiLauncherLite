// SPDX-License-Identifier: GPL-3.0-only
/*
 *  What a player can change about one version: how much memory it gets, and the arguments it is
 *  started with. An instance with nothing here uses the launcher's settings.
 */

#pragma once

#include <QString>

namespace Komorebi {

struct InstanceConfig {
    QString displayName;    ///< what the list shows; empty means the version id
    QString description;
    int launchCount = 0;
    QString lastLaunched;   ///< ISO date of the last launch

    // overrides; 0 or empty means "use the launcher's setting"
    int memoryMb = 0;
    QString jvmArguments;
    QString gameArguments;

    static InstanceConfig load(const QString& versionDirectory);
    void save(const QString& versionDirectory) const;
};

}  // namespace Komorebi
