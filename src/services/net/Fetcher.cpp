// SPDX-License-Identifier: GPL-3.0-only
#include "services/net/Fetcher.h"

#include "services/net/DownloadSite.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSaveFile>
#include <QThread>
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
constexpr int kTimeoutMs = 30000;
/// Small documents from metadata services can be slow without being broken.
constexpr int kJsonTimeoutMs = 60000;

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
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) KomorebiLite/0.1 (+https://github.com/KomorebiLauncher/KomorebiLauncherLite)"));
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
namespace {

JsonResult requestJson(const QString& url, const QList<QPair<QString, QString>>& headers, const QByteArray* body)
{
    JsonResult result;

    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KomorebiLite/0.1"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    for (const auto& header : headers)
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());

    QNetworkReply* reply = body ? manager().post(request, *body) : manager().get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    timer.start(kJsonTimeoutMs);
    loop.exec();

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && result.body.isEmpty())
        result.error = reply->errorString();
    reply->deleteLater();

    // Nothing came back at all: a metadata service that is slow once usually answers on a second
    // try, and failing an install over it is worse than waiting.
    if (result.status == 0)
        return requestJson(url, headers, body);

    // A service that answers with a JSON error body is still an answer: the caller reads the error
    // code out of it, which is how "authorization_pending" arrives.
    result.ok = result.status >= 200 && result.status < 300;
    return result;
}

}  // namespace

JsonResult postJson(const QString& url, const QByteArray& body, const QList<QPair<QString, QString>>& headers)
{
    return requestJson(url, headers, &body);
}

JsonResult getJson(const QString& url, const QList<QPair<QString, QString>>& headers)
{
    return requestJson(url, headers, nullptr);
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

    // The policy decides how many files run at once and how fast.
    const DownloadPolicy& policy = downloadPolicy();
    if (concurrency <= 0)
        concurrency = policy.concurrency;

    const int total = jobs.size();
    int doneCount = summary.skipped;
    if (onProgress)
        onProgress(doneCount, total);
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
            // The mirror answers the same paths under its own host. When it does not have a file,
            // the original server is tried before the file is called failed.
            const QString preferred = preferredUrl(job.url);
            QNetworkReply* reply = manager().get(makeRequest(preferred));
            reply->setProperty("retryUrl", preferred != job.url ? job.url : QString());
            ++active;

            QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, job] {
                --active;
                const QString retryUrl = reply->property("retryUrl").toString();
                if (reply->error() != QNetworkReply::NoError && !retryUrl.isEmpty()) {
                    reply->deleteLater();
                    QNetworkReply* second = manager().get(makeRequest(retryUrl));
                    second->setProperty("retryUrl", QString());
                    QObject::connect(second, &QNetworkReply::finished, &loop, [&, second, job] {
                        const auto finish = [&](QNetworkReply* answer) {
                            --active;
                            if (answer->error() != QNetworkReply::NoError) {
                                summary.failed << QStringLiteral("%1 (%2)").arg(job.target, answer->errorString());
                            } else {
                                const QByteArray data = answer->readAll();
                                QString error;
                                if (!writeAtomically(job.target, data, &error))
                                    summary.failed << QStringLiteral("%1 (%2)").arg(job.target, error);
                                else if (!job.sha1.isEmpty()
                                         && QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()) != job.sha1)
                                    summary.failed << QStringLiteral("%1 (hash mismatch)").arg(job.target);
                                else
                                    ++summary.downloaded;
                            }
                            answer->deleteLater();
                            ++doneCount;
                            if (onProgress)
                                onProgress(doneCount, total);
                            if (next >= pending.size() && active == 0)
                                loop.quit();
                            else
                                startNext();
                        };
                        finish(second);
                    });
                    ++active;
                    return;
                }
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
                if (policy.speedLimitKb > 0) {
                    const qint64 bytes = reply->bytesAvailable();
                    const qint64 budgetMs = (bytes / 1024) * 1000 / policy.speedLimitKb;
                    if (budgetMs > 0)
                        QThread::msleep(static_cast<unsigned long>(qMin<qint64>(budgetMs, 5000)));
                }
                ++doneCount;
                if (onProgress)
                    onProgress(doneCount, total);
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
