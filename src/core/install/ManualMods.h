// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The files of a pack that its authors let nobody download but the CurseForge site.
 *
 *  A CurseForge pack lists its mods by project and file id; most files come with a download link,
 *  and a few are answered with 403 because the author ticked "no third-party downloads". No launcher
 *  may fetch those, so installing the pack in full is not possible -- but installing everything else
 *  is, as long as the player is told exactly what is missing and where each file's page is.
 *
 *  The list is written beside the version (manual-mods.json). The player downloads those files in a
 *  browser and drops them into `<data>/hokora`; they are moved into the pack's mods folder once the
 *  bytes match the sha1 the pack's file listing gave, so a wrong version of a mod is never taken for
 *  the right one. A name that matches but whose contents do not is reported, not accepted.
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Komorebi {

struct ManualMod {
    QString name;      ///< the project's name, as CurseForge spells it
    QString version;   ///< the file's own name for itself, usually with the version in it
    QString fileName;  ///< the file to put into hokora
    QString sha1;      ///< lowercase hex of the file the pack asked for; empty when the API gave none
    QString pageUrl;   ///< the CurseForge page of this exact file
    int projectId = 0;
    int fileId = 0;
};

/// The list kept beside a version; empty when nothing is pending.
QList<ManualMod> readManualMods(const QString& versionDirectory);

/// Writes the list, or removes the file when nothing is pending any more.
bool writeManualMods(const QString& versionDirectory, const QList<ManualMod>& mods);

struct HokoraHarvest {
    int taken = 0;          ///< files that matched and were moved into the mods folder
    QStringList rejected;   ///< files in hokora that carry a wanted name but not the wanted bytes
};

/// Reads the list beside the version, harvests what hokora holds, and writes back what is left.
HokoraHarvest harvestHokora(const QString& versionDirectory);

}  // namespace Komorebi
