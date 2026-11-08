// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Where the files come from.
 *
 *  Mojang's own servers are the source of truth, but they are slow from some places. A mirror
 *  answers at the same paths under a different host, so switching is a rewrite of the url, not a
 *  different code path -- and when the mirror does not have something, the original is tried.
 */

#pragma once

#include <QString>

namespace Komorebi {

enum class DownloadSite {
    Official,
    Mirror,  ///< BMCLAPI, the mirror most Chinese launchers use
};

struct DownloadPolicy {
    DownloadSite site = DownloadSite::Official;
    int concurrency = 8;   ///< how many files at once
    int speedLimitKb = 0;  ///< 0: no limit
};

/// The policy every fetch uses. Set once at startup.
void setDownloadPolicy(const DownloadPolicy& policy);
const DownloadPolicy& downloadPolicy();

/// The mirror form of a url, or the same url when there is no mapping.
QString mirrorUrl(const QString& url);

/// The url to try first for a request, given the policy in force.
QString preferredUrl(const QString& url);

}  // namespace Komorebi
