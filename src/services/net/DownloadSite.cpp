// SPDX-License-Identifier: GPL-3.0-only
#include "services/net/DownloadSite.h"

#include <QUrl>

namespace Komorebi {

namespace {

DownloadPolicy& policy()
{
    static DownloadPolicy instance;
    return instance;
}

/// Hosts whose objects are mirrored at the mirror root, path unchanged.
const QStringList kObjectHosts = {
    QStringLiteral("piston-meta.mojang.com"),
    QStringLiteral("piston-data.mojang.com"),
    QStringLiteral("launcher.mojang.com"),
    QStringLiteral("launchermeta.mojang.com"),
};

/// Hosts whose files live in one maven tree, which the mirror exposes under /maven.
const QStringList kMavenHosts = {
    QStringLiteral("libraries.minecraft.net"),
    QStringLiteral("maven.minecraftforge.net"),
    QStringLiteral("maven.neoforged.net"),
    QStringLiteral("maven.fabricmc.net"),
    QStringLiteral("maven.quiltmc.org"),
    QStringLiteral("repo1.maven.org"),
};

/// Assets: the mirror serves them under /assets, the same layout the official host uses.
const QStringList kAssetHosts = {
    QStringLiteral("resources.download.minecraft.net"),
};

QUrl validBase(const QString& raw)
{
    const QUrl base(raw.trimmed(), QUrl::TolerantMode);
    const QString scheme = base.scheme().toLower();
    if (!base.isValid() || base.host().isEmpty() || (scheme != QLatin1String("https") && scheme != QLatin1String("http")))
        return {};
    return base;
}

QString withPrefix(const QUrl& source, const QUrl& base, const QString& prefix)
{
    QUrl out = base;
    QString path = out.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    out.setPath(path + prefix + source.path());
    if (source.hasQuery())
        out.setQuery(source.query());
    return out.toString(QUrl::FullyEncoded);
}

}  // namespace

void setDownloadPolicy(const DownloadPolicy& value)
{
    policy() = value;
}

const DownloadPolicy& downloadPolicy()
{
    return policy();
}

QString mirrorUrl(const QString& url, const QString& mirrorBase)
{
    const QUrl source(url, QUrl::TolerantMode);
    if (!source.isValid() || source.host().isEmpty())
        return {};

    const QUrl base = validBase(mirrorBase.isEmpty() ? policy().mirrorBase : mirrorBase);
    if (base.host().isEmpty())
        return {};

    const QString host = source.host().toLower();
    if (kMavenHosts.contains(host))
        return withPrefix(source, base, QStringLiteral("/maven"));
    if (kAssetHosts.contains(host))
        return withPrefix(source, base, QStringLiteral("/assets"));
    if (kObjectHosts.contains(host))
        return withPrefix(source, base, QString());
    return {};
}

QStringList candidateUrls(const QString& url)
{
    QStringList candidates;

    const QString mirror = mirrorUrl(url);
    const MirrorMode mode = policy().mode;

    const bool mirrorFirst = mode == MirrorMode::MirrorThenOfficial || mode == MirrorMode::MirrorOnly;
    const bool officialAllowed = mode != MirrorMode::MirrorOnly;
    const bool mirrorAllowed = mode != MirrorMode::OfficialOnly && !mirror.isEmpty();

    if (mirrorFirst) {
        if (mirrorAllowed)
            candidates << mirror;
        if (officialAllowed)
            candidates << url;
    } else {
        if (officialAllowed)
            candidates << url;
        if (mirrorAllowed)
            candidates << mirror;
    }

    // Mirror-only means mirror-only: no quiet fallback to the official host. If the mirror cannot even
    // be built (no base, or a host nobody mirrors) the official url is all that is left.
    if (candidates.isEmpty())
        candidates << url;
    return candidates;
}

}  // namespace Komorebi
