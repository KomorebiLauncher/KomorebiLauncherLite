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
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

#include "services/net/DownloadSite.h"

namespace Komorebi {

namespace {

/// How long a transfer may go without receiving anything before that source is given up on. It is an
/// idle timeout, not a deadline for the whole file: a hundred megabytes on a slow link is not an
/// error, a connection that stopped delivering is.
constexpr int kTimeoutMs = 30000;

/// Small documents from metadata services can be slow without being broken.
constexpr int kJsonTimeoutMs = 60000;

/// Where the player's cancel button writes; nullptr means nobody is watching. The pointer itself is
/// written from the page thread and read from the download thread, so it needs to be atomic too: a
/// plain pointer was a data race that could be seen as garbage exactly when a download was running.
std::atomic<std::atomic<bool>*> cancellationFlag{ nullptr };

/**
 * The one thread that owns the one connection pool.
 *
 * A QNetworkAccessManager, and every reply, timer and socket notifier it hands out, belongs to the
 * thread that created it, and Qt means it: using one from another thread is not a slow path, it is
 * undefined behaviour. This launcher cannot promise that, because the calls are blocking and the
 * callers are everywhere -- a search runs on a thread that lives for one request, the install that
 * follows runs on a different one, and the command-line hooks run on the window's own thread. The
 * old code kept the manager in a function-local static, so it belonged to whichever thread happened
 * to ask first, which was a download thread that had already ended by the time the player pressed
 * install. That is the click that closed the launcher: the reply was built as a child of a manager
 * whose thread was gone.
 *
 * The fix is a thread of its own, alive for as long as the process, that owns the manager and the
 * event loop it needs. Every entry point below hands its work over and waits; a call made from the
 * network thread itself (an installer that fetches inside a download, say) runs inline, because
 * waiting on itself would deadlock the only thread that could complete the work.
 */
class NetworkThread : public QThread {
   public:
    NetworkThread()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        start();
        // Nothing may be handed over before run() has created the objects it runs on.
        m_ready.wait(lock, [this] { return m_relay != nullptr; });
    }

    /// Runs `work` on the network thread and returns once it has finished.
    void run(std::function<void()> work)
    {
        if (QThread::currentThread() == this) {
            work();
            return;
        }

        QSemaphore finished;
        QObject* relay = m_relay;
        if (relay
            && QMetaObject::invokeMethod(
                relay, [work, &finished] {
                    work();
                    finished.release();
                },
                Qt::QueuedConnection)) {
            finished.acquire();
            return;
        }

        // The thread never starts or stops on its own, so this only happens while the process is
        // going down. Running the work here is wrong-threaded, but a hang would be worse.
        work();
    }

    QNetworkAccessManager& manager() const { return *m_manager; }

   protected:
    void run() override
    {
        QNetworkAccessManager manager;
        QObject relay;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_manager = &manager;
            m_relay = &relay;
        }
        m_ready.notify_all();
        exec();
    }

   private:
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    QNetworkAccessManager* m_manager = nullptr;
    QObject* m_relay = nullptr;
};

NetworkThread& networkThread()
{
    // Deliberately never destroyed. It has to outlive anything that can still be downloading, and
    // tearing a QNetworkAccessManager down after QCoreApplication is gone is a crash on exit -- the
    // process is ending at that point anyway.
    static NetworkThread* instance = new NetworkThread;
    return *instance;
}

/// Only valid on the network thread: every caller goes through networkThread().
QNetworkAccessManager& manager()
{
    return networkThread().manager();
}

QNetworkRequest makeRequest(const QString& url)
{
    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) KomorebiLite/0.1 (+https://github.com/KomorebiLauncher/KomorebiLauncherLite)"));
    return request;
}

/// A reply read to completion, with a way out when the server stops sending.
///
/// `timeoutMs` is how long the transfer may go without receiving anything, not how long the whole
/// request may take: a large file on a slow link is not a failure, a connection that has gone silent
/// is. The length the server declared is handed back, so a caller can tell a whole body from the part
/// of one that arrived before the peer went away.
QByteArray readReply(QNetworkReply* reply, int timeoutMs, QString* error, int* status = nullptr,
                     qint64* contentLength = nullptr)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    const auto restart = [&] { timer.start(timeoutMs); };

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, restart);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    restart();
    loop.exec();

    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const qint64 declared = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (contentLength)
        *contentLength = declared;
    const QByteArray data = reply->readAll();
    // A body that arrived in part is not a body. A peer that closed early, a transfer that stalled
    // and a request that was aborted all leave bytes behind; writing them out would produce a file
    // that looks installed and is not, which is exactly the failure this launcher cannot afford.
    if (reply->error() != QNetworkReply::NoError) {
        *error = QStringLiteral("network: %1").arg(reply->errorString());
    } else if (declared > 0 && data.size() != declared) {
        *error = QStringLiteral("network: the server sent %1 of %2 bytes").arg(data.size()).arg(declared);
    }
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

/// The bodies below run on the network thread; the public functions only hand them over.

JsonResult getJsonHere(const QString& url, const QList<QPair<QString, QString>>& headers)
{
    JsonResult result;
    for (const QString& candidate : candidateUrls(url)) {
        if (requestCancelled()) {
            result.error = QStringLiteral("cancelled");
            return result;
        }
        result = requestOnce(candidate, headers, nullptr);
        if (result.status >= 200 && result.status < 400)
            return result;
    }
    return result;
}

FetchResult fetchFileHere(const QString& url, const QString& target, const QString& sha1)
{
    FetchResult result;

    if (requestCancelled()) {
        result.error = QStringLiteral("cancelled");
        return result;
    }

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
            // An error is an error even when part of the body arrived: `readReply` also refuses a
            // body shorter than the length the server declared, so a truncated download never
            // reaches the disk as if it had worked.
            if (!error.isEmpty() || data.isEmpty() || (status >= 400 && status < 600)) {
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

FetchSummary fetchManyHere(const QList<FetchJob>& jobs, int concurrency,
                           const std::function<void(int done, int total)>& onProgress = {})
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
    QList<QNetworkReply*> inFlight;
    bool aborted = false;

    std::function<void()> startNext;

    const auto finishOne = [&](bool success, const QString& failure) {
        if (!success)
            summary.failed << failure;
        --active;
        ++doneCount;
        if (onProgress)
            onProgress(doneCount, total);
        // A cancelled install queues nothing new: filling the remaining jobs one by one from here is
        // what made cancelling a 3000-file install walk three thousand stack frames deep.
        if (aborted) {
            if (active == 0)
                loop.quit();
            return;
        }
        if (next >= pending.size() && active == 0)
            loop.quit();
        else
            startNext();
    };

    std::function<void(const FetchJob&, int, int)> attempt;  // job, candidate index, retries used

    startNext = [&] {
        while (!aborted && active < qMax(1, concurrency) && next < pending.size()) {
            const FetchJob job = pending.at(next++);
            ++active;
            attempt(job, 0, 0);
        }
    };

    attempt = [&](const FetchJob& job, int index, int retries) {
        if (aborted)
            return;
        if (requestCancelled()) {
            aborted = true;
            finishOne(false, QStringLiteral("%1 (cancelled)").arg(job.target));
            return;
        }
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
        inFlight.append(reply);
        // The reply arrives later, when this call has returned: everything the handler needs has to be
        // captured by value. Reaching back to `candidates` here read freed memory, which is what made
        // a failed download take the whole launcher down with it.
        QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, job, index, retries, candidateCount, fallbackSource] {
            inFlight.removeOne(reply);
            if (aborted) {
                reply->deleteLater();
                return;
            }
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

            const qint64 declared = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            const QByteArray data = reply->readAll();
            reply->deleteLater();

            if (declared > 0 && data.size() != declared) {
                // The peer promised a length and broke off before sending it: the same retry as a 4xx,
                // rather than a file that is silently short.
                if (retries < 2) {
                    QTimer::singleShot(500, &loop, [&, job, index, retries] { attempt(job, index, retries + 1); });
                    return;
                }
                attempt(job, index + 1, 0);
                return;
            }

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

    // Cancel has to reach the downloads that are already running, not only the ones not started yet:
    // a queue of three thousand files otherwise kept the window waiting for thirty-second timeouts.
    QTimer cancelWatch;
    cancelWatch.setInterval(100);
    QObject::connect(&cancelWatch, &QTimer::timeout, &loop, [&] {
        if (!requestCancelled())
            return;
        aborted = true;
        const QList<QNetworkReply*> running = inFlight;
        inFlight.clear();
        for (QNetworkReply* reply : running) {
            // The handler below reaches into this function's stack: disconnecting it first means the
            // aborted reply cannot arrive after the loop is gone.
            QObject::disconnect(reply, nullptr, &loop, nullptr);
            reply->abort();
            reply->deleteLater();
        }
        loop.quit();
    });
    cancelWatch.start();

    startNext();
    if (active > 0 && !aborted)
        loop.exec();
    cancelWatch.stop();

    // A cancelled install reports what it managed to do; the caller already knows it was stopped.
    if (aborted)
        return summary;

    // Last pass for the files whose only answer was a copy the hash does not describe: fetch them
    // without the check. Without this an install fails while a usable file is one request away; with
    // it, the install log records that the copy was accepted.
    if (!mismatched.isEmpty()) {
        QList<FetchJob> relaxed;
        relaxed.reserve(mismatched.size());
        for (const auto& job : mismatched)
            relaxed.append({ job.url, job.target, {} });
        const FetchSummary second = fetchManyHere(relaxed, concurrency);
        summary.downloaded += second.downloaded;
        summary.failed += second.failed;
    }
    return summary;
}

BytesResult fetchBytesHere(const QString& url)
{
    BytesResult result;
    const JsonResult answer = getJsonHere(url, { { QStringLiteral("Accept"), QStringLiteral("application/json") } });
    result.ok = answer.status >= 200 && answer.status < 300;
    result.data = answer.body;
    if (!result.ok)
        result.error = answer.error.isEmpty() ? QStringLiteral("no answer for %1").arg(url) : answer.error;
    return result;
}

}  // namespace

void setCancellationFlag(std::atomic<bool>* flag)
{
    cancellationFlag.store(flag);
}

bool requestCancelled()
{
    const std::atomic<bool>* flag = cancellationFlag.load();
    return flag && flag->load();
}

JsonResult postJson(const QString& url, const QByteArray& body, const QList<QPair<QString, QString>>& headers)
{
    // A POST goes where it says: mirrors hold files, not services.
    JsonResult result;
    networkThread().run([&] { result = requestOnce(url, headers, &body); });
    return result;
}

JsonResult getJson(const QString& url, const QList<QPair<QString, QString>>& headers)
{
    JsonResult result;
    networkThread().run([&] { result = getJsonHere(url, headers); });
    return result;
}

FetchResult fetchFile(const QString& url, const QString& target, const QString& sha1)
{
    FetchResult result;
    networkThread().run([&] { result = fetchFileHere(url, target, sha1); });
    return result;
}

FetchSummary fetchMany(const QList<FetchJob>& jobs, int concurrency, const std::function<void(int done, int total)>& onProgress)
{
    FetchSummary summary;
    networkThread().run([&] { summary = fetchManyHere(jobs, concurrency, onProgress); });
    return summary;
}

BytesResult fetchBytes(const QString& url)
{
    BytesResult result;
    networkThread().run([&] { result = fetchBytesHere(url); });
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
