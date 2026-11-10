// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Carrying the data directory to a new place.
 *
 *  Moving it is the player's own request, so it happens while they watch: a progress callback per
 *  file lets the settings dialog show what is going on and lets them stop halfway, and the caller
 *  only switches over once the copy reported success. Nothing here deletes the old directory.
 */

#pragma once

#include <QString>

#include <functional>

namespace Komorebi {

struct DataCopyResult {
    int files = 0;
    qint64 bytes = 0;
    QString error;  ///< set when the copy stopped because something could not be written
    bool cancelled = false;
};

/// Called as files land: return false to stop the copy.
using DataCopyProgress = std::function<bool(const QString& path, int files, qint64 bytes)>;

/**
 * Copies everything `from` holds into `to`, creating directories as needed.
 *
 * The scratch directory (`tmp`) and the data-root pointer are left out: the first is throwaway and
 * the second belongs to the default location, not to the data it points at.
 */
DataCopyResult copyDataDirectory(const QString& from, const QString& to, const DataCopyProgress& progress = {});

}  // namespace Komorebi
