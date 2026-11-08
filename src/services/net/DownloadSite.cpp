// SPDX-License-Identifier: GPL-3.0-only
#include "services/net/DownloadSite.h"

namespace Komorebi {

namespace {

DownloadPolicy& policy()
{
    static DownloadPolicy instance;
    return instance;
}

struct Rewrite {
    const char* from;
    const char* to;
};

// BMCLAPI answers the same paths under its own host. The order matters: the more specific hosts
// come first, because the generic one would swallow them.
constexpr Rewrite kRewrites[] = {
    { "https://resources.download.minecraft.net", "https://bmclapi2.bangbang93.com/assets" },
    { "https://libraries.minecraft.net", "https://bmclapi2.bangbang93.com/maven" },
    { "https://piston-meta.mojang.com", "https://bmclapi2.bangbang93.com" },
    { "https://launchermeta.mojang.com", "https://bmclapi2.bangbang93.com" },
    { "https://launcher.mojang.com", "https://bmclapi2.bangbang93.com" },
};

}  // namespace

void setDownloadPolicy(const DownloadPolicy& value)
{
    policy() = value;
}

const DownloadPolicy& downloadPolicy()
{
    return policy();
}

QString mirrorUrl(const QString& url)
{
    for (const auto& rewrite : kRewrites) {
        const QString from = QString::fromLatin1(rewrite.from);
        if (url.startsWith(from))
            return QString::fromLatin1(rewrite.to) + url.mid(from.size());
    }
    return url;
}

QString preferredUrl(const QString& url)
{
    return policy().site == DownloadSite::Mirror ? mirrorUrl(url) : url;
}

}  // namespace Komorebi
