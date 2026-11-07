// SPDX-License-Identifier: GPL-3.0-only
#include "services/net/Fetcher.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCryptographicHash>

namespace Komorebi {

QString fileSha1(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

FetchResult fetchFile(const QString& url, const QString& target, const QString& sha1)
{
    FetchResult result;

    if (QFileInfo::exists(target) && !sha1.isEmpty() && fileSha1(target) == sha1) {
        result.ok = true;
        result.alreadyPresent = true;
        result.bytes = QFileInfo(target).size();
        return result;
    }

    const QString directory = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(directory)) {
        result.error = QStringLiteral("cannot create %1 (exists: %2)").arg(directory, QFileInfo(directory).exists() ? QStringLiteral("yes") : QStringLiteral("no"));
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Komorebi/0.1"));
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.error = QStringLiteral("network: %1").arg(reply->errorString());
        reply->deleteLater();
        return result;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();
    result.bytes = data.size();

    if (!sha1.isEmpty()) {
        const QString actual = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
        if (actual != sha1) {
            result.error = QStringLiteral("hash mismatch: expected %1, got %2").arg(sha1, actual);
            return result;
        }
    }

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("write %1: %2").arg(target, file.errorString());
        return result;
    }
    file.write(data);
    file.close();

    result.ok = true;
    return result;
}

}  // namespace Komorebi
