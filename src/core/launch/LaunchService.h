// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Turning "launch this version" into a command line.
 *
 *  One place knows how the settings, the instance's own overrides and the version's requirements
 *  come together, so the window and the command line behave the same way.
 */

#pragma once

#include <QString>

#include "core/Settings.h"
#include "core/launch/LaunchPlan.h"

namespace Komorebi {

struct PreparedLaunch {
    LaunchPlan plan;
    QString versionId;
    QString baseId;
    QString error;

    bool ok() const { return error.isEmpty() && plan.valid() && plan.missingFiles.isEmpty(); }
};

/// Builds the plan for an installed version, applying the settings and the instance's overrides.
PreparedLaunch prepareLaunch(const Settings& settings, const QString& versionId, const QString& gameDirectory = {});

}  // namespace Komorebi
