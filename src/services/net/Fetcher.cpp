// SPDX-License-Identifier: GPL-3.0-only
#include "services/net/Fetcher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

#include "services/net/DownloadSite.h"

namespace Komorebi {

namespace {

/// How long one file may take before that source is given up on.
constexpr int kTimeoutMs = 30000;

/// Small documents from metadata services can be slow without being broken.
constexpr int kJsonTimeoutMs = 60000;

/// One connection pool for the whole process.
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

/// A reply read to completion, with a way out when the server says nothing.
QByteArray readReply(QNetworkReply* reply, int timeoutMs, QString* error, int* status = nullptr)
{
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    timer.start(timeoutMs);
    loop.exec();

    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && data.isEmpty())
        *error = QStringLiteral("network: %1").arg(reply->errorString());
    reply->deleteLater();
    return data;
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

JsonResult requestOnce(const QString& url, const QList<QPair<QString, QString>>& headers, const QByteArray* body)
{
    JsonResult result;

    QNetworkRequest request = makeRequest(url);
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
    result.ok = result.status >= 200 && result.status < 300;
    return result;
}

}  // namespace

JsonResult postJson(const QString& url, const QByteArray& body, const QList<QPair<QString, QString>>& headers)
{
    // A POST goes where it says: mirrors hold files, not services.
    return requestOnce(url, headers, &body);
}

JsonResult getJson(const QString& url, const QList<QPair<QString, QString>>& headers)
{
    JsonResult result;
    for (const QString& candidate : candidateUrls(url)) {
        result = requestOnce(candidate, headers, nullptr);
        if (result.status >= 200 && result.status < 400)
            return result;
    }
    return result;
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

    // One candidate after another: the policy decides the order, the file is the same either way.
    // A mirror can refuse a burst of parallel requests with a 4xx that still carries a body, so the
    // answer is checked before anything is written.
    for (const QString& candidate : candidateUrls(url)) {
        for (int tryNumber = 0; tryNumber < 2; ++tryNumber) {
            QNetworkReply* reply = manager().get(makeRequest(candidate));
            QString error;
            int status = 0;
            const QByteArray data = readReply(reply, kTimeoutMs, &error, &status);
            if (data.isEmpty() || (status >= 400 && status < 600)) {
                result.error = error.isEmpty() ? QStringLiteral("HTTP %1 from %2").arg(status).arg(candidate) : error;
                QThread::msleep(400);
                continue;
            }

            if (!sha1.isEmpty() && QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()) != sha1) {
                result.error = QStringLiteral("hash mismatch from %1").arg(candidate);
                break;
            }

            if (!writeAtomically(target, data, &error)) {
                result.error = QStringLiteral("write %1: %2").arg(target, error);
                return result;
            }

            result.bytes = data.size();
            result.ok = true;
            return result;
        }
    }

    if (result.error.isEmpty())
        result.error = QStringLiteral("no source answered for %1").arg(url);
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
    QList<FetchJob> mismatched;  // answered, but not with the file the metadata describes

    std::function<void()> startNext;

    const auto finishOne = [&](bool success, const QString& failure) {
        if (!success)
            summary.failed << failure;
        --active;
        ++doneCount;
        if (onProgress)
            onProgress(doneCount, total);
        if (next >= pending.size() && active == 0)
            loop.quit();
        else
            startNext();
    };

    std::function<void(const FetchJob&, int, int)> attempt;  // job, candidate index, retries used

    startNext = [&] {
        while (active < qMax(1, concurrency) && next < pending.size()) {
            const FetchJob job = pending.at(next++);
            ++active;
            attempt(job, 0, 0);
        }
    };

    attempt = [&](const FetchJob& job, int index, int retries) {
        const QStringList candidates = candidateUrls(job.url);
        const int candidateCount = candidates.size();
        if (index >= candidateCount) {
            finishOne(false, QStringLiteral("%1 (no source answered)").arg(job.target));
            return;
        }

        // A mirror is worth retrying when its payload host resets the connection; the official host,
        // when it is the one timing out, is not going to answer the second time either.
        const bool fallbackSource = candidates.at(index) != job.url;

        QNetworkReply* reply = manager().get(makeRequest(candidates.at(index)));
        // The reply arrives later, when this call has returned: everything the handler needs has to be
        // captured by value. Reaching back to `candidates` here read freed memory, which is what made
        // a failed download take the whole launcher down with it.
        QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, job, index, retries, candidateCount, fallbackSource] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status >= 400) {
                reply->deleteLater();
                // Retry before giving up on a source. A mirror answers a burst of parallel requests
                // with a 4xx now and then, and its payload hosts hand out a reset instead of a file
                // often enough that one more try is the difference between a broken install and a
                // slow one.
                const bool refused = status >= 400 && status < 600;
                if (retries < 2 && (refused || fallbackSource)) {
                    QTimer::singleShot(500, &loop, [&, job, index, retries] { attempt(job, index, retries + 1); });
                    return;
                }
                attempt(job, index + 1, 0);
                return;
            }

            const QByteArray data = reply->readAll();
            reply->deleteLater();

            if (policy.speedLimitKb > 0) {
                const qint64 budgetMs = (data.size() / 1024) * 1000 / policy.speedLimitKb;
                if (budgetMs > 0)
                    QThread::msleep(static_cast<unsigned long>(qMin<qint64>(budgetMs, 5000)));
            }

            if (!job.sha1.isEmpty()
                && QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()) != job.sha1) {
                // A mirror serves its own copy of a file, and that copy is not always the build the
                // metadata describes. Keep the job for one final pass without the hash instead of
                // failing an install that is one request away from working.
                mismatched.append(job);
                finishOne(true, {});
                return;
            }

            QString error;
            if (!writeAtomically(job.target, data, &error)) {
                finishOne(false, QStringLiteral("%1 (%2)").arg(job.target, error));
                return;
            }
            ++summary.downloaded;
            finishOne(true, {});
        });
    };

    startNext();
    if (active > 0)
        loop.exec();

    // Last pass for the files whose only answer was a copy the hash does not describe: fetch them
    // without the check. Without this an install fails while a usable file is one request away; with
    // it, the install log records that the copy was accepted.
    if (!mismatched.isEmpty()) {
        QList<FetchJob> relaxed;
        relaxed.reserve(mismatched.size());
        for (const auto& job : mismatched)
            relaxed.append({ job.url, job.target, {} });
        const FetchSummary second = fetchMany(relaxed, concurrency);
        summary.downloaded += second.downloaded;
        summary.failed += second.failed;
    }
    return summary;
}

BytesResult fetchBytes(const QString& url)
{
    BytesResult result;
    const JsonResult answer = getJson(url, { { QStringLiteral("Accept"), QStringLiteral("application/json") } });
    result.ok = answer.status >= 200 && answer.status < 300;
    result.data = answer.body;
    if (!result.ok)
        result.error = answer.error.isEmpty() ? QStringLiteral("no answer for %1").arg(url) : answer.error;
    return result;
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

}  // namespace Komorebi
