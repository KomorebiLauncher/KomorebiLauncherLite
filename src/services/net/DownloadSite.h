// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Where the files come from.
 *
 *  Mojang's servers are the source of truth; a mirror answers the same content under a different
 *  host, and which of the two is faster depends on where the player is and on the day. So the
 *  order is a setting, and every download walks the candidates until one of them answers.
 *
 *  The shapes matter: BMCLAPI serves the game files at its root, the assets under /assets and the
 *  libraries under /maven, so one prefix cannot stand for all three. The mapping follows the same
 *  grouping the full launcher uses.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Komorebi {

/// How the official host and the mirror are ordered against each other.
enum class MirrorMode {
    OfficialOnly = 0,
    OfficialThenMirror = 1,
    MirrorThenOfficial = 2,
    MirrorOnly = 3,
};

struct DownloadPolicy {
    MirrorMode mode = MirrorMode::OfficialThenMirror;
    QString mirrorBase = QStringLiteral("https://bmclapi2.bangbang93.com");
    int concurrency = 8;
    int speedLimitKb = 0;
};

/// The policy every fetch uses. Set once at startup.
void setDownloadPolicy(const DownloadPolicy& policy);
const DownloadPolicy& downloadPolicy();

/// The urls to try for one request, best first; always contains the original.
QStringList candidateUrls(const QString& url);

/// The mirror form of a url, or an empty string when the host is not mirrored.
QString mirrorUrl(const QString& url, const QString& mirrorBase = {});

}  // namespace Komorebi
