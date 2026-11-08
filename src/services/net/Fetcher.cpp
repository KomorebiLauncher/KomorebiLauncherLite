// SPDX-License-Identifier: GPL-3.0-only
#include "services/net/Fetcher.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCryptographicHash>
#include <functional>

namespace Komorebi {

namespace {
/// How long a single file may take before the download is given up on.
constexpr int kTimeoutMs = 20000;

/// One connection pool for everything the launcher fetches.
QNetworkAccessManager& manager()
{
    static QNetworkAccessManager instance;
    return instance;
}

QNetworkRequest makeRequest(const QString& url)
{
    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Komorebi/0.1"));
    return request;
}

bool writeAtomically(const QString& target, const QByteArray& data, QString* error)
{
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    if (file.write(data) != data.size()) {
        *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

}  // namespace

BytesResult fetchBytes(const QString& url)
{
    BytesResult result;

    QNetworkReply* reply = manager().get(makeRequest(url));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    timer.start(kTimeoutMs);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.error = QStringLiteral("network: %1").arg(reply->errorString());
        reply->deleteLater();
        return result;
    }

    result.data = reply->readAll();
    reply->deleteLater();
    result.ok = true;
    return result;
}
FetchSummary fetchMany(const QList<FetchJob>& jobs, int concurrency, const std::function<void(int done, int total)>& onProgress)
{
    FetchSummary summary;

    // What is already on disk does not need a request: an interrupted install resumes for free.
    QList<FetchJob> pending;
    pending.reserve(jobs.size());
    for (const auto& job : jobs) {
        if (!job.sha1.isEmpty() && QFileInfo::exists(job.target) && fileSha1(job.target) == job.sha1) {
            ++summary.skipped;
            continue;
        }
        pending.append(job);
    }

    const int total = jobs.size();
    int done = summary.skipped;
    if (onProgress)
        onProgress(done, total);
    if (pending.isEmpty())
        return summary;

    QEventLoop loop;
    int active = 0;
    int next = 0;

    // it calls itself when a request finishes, so it needs a name before its body
    std::function<void()> startNext;
    startNext = [&] {
        while (active < qMax(1, concurrency) && next < pending.size()) {
            const FetchJob job = pending.at(next++);
            QNetworkReply* reply = manager().get(makeRequest(job.url));
            ++active;

            QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, job] {
                --active;
                if (reply->error() != QNetworkReply::NoError) {
                    summary.failed << QStringLiteral("%1 (%2)").arg(job.target, reply->errorString());
                } else {
                    const QByteArray data = reply->readAll();
                    QString error;
                    if (!writeAtomically(job.target, data, &error)) {
                        summary.failed << QStringLiteral("%1 (%2)").arg(job.target, error);
                    } else if (!job.sha1.isEmpty()) {
                        const QString actual = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
                        if (actual != job.sha1)
                            summary.failed << QStringLiteral("%1 (hash mismatch)").arg(job.target);
                        else
                            ++summary.downloaded;
                    } else {
                        ++summary.downloaded;
                    }
                }
                reply->deleteLater();
                ++done;
                if (onProgress)
                    onProgress(done, total);
                if (next >= pending.size() && active == 0)
                    loop.quit();
                else
                    startNext();
            });
        }
    };

    startNext();
    if (active > 0)
        loop.exec();
    return summary;
}
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

    QNetworkReply* reply = manager().get(makeRequest(url));

    // A server that accepts the connection and then says nothing would otherwise hang the launcher
    // forever: the loop needs its own way out.
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    timer.start(kTimeoutMs);
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

    // QSaveFile writes beside the target and renames: a crash leaves the old file, never half of a
    // new one. The byte count is checked because a full disk reports success otherwise.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("write %1: %2").arg(target, file.errorString());
        return result;
    }
    if (file.write(data) != data.size()) {
        result.error = QStringLiteral("short write to %1: %2").arg(target, file.errorString());
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.error = QStringLiteral("could not replace %1: %2").arg(target, file.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace Komorebi
