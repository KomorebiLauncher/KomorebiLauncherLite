// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Downloads. One request at a time is easy to write and slow to use: every file opened a new
 *  connection, and a version has thousands of files. This keeps one connection pool and runs a
 *  few requests at once, which is what makes an install take minutes instead of half an hour.
 *
 *  Everything here is blocking and may be called from any thread: the pool belongs to a thread of
 *  its own that outlives the callers, and each call is carried out there and waited for. That is
 *  also why a call can be made from a thread that is about to end -- a page that searches on a
 *  throw-away thread, say -- which the pool itself would not allow.
 */

#pragma once

#include <QByteArray>
#include <QPair>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace Komorebi {

struct FetchResult {
    bool ok = false;
    bool alreadyPresent = false;
    QString error;
    qint64 bytes = 0;
};

struct BytesResult {
    bool ok = false;
    QString error;
    QByteArray data;
};

/// One file to fetch.
struct FetchJob {
    QString url;
    QString target;
    QString sha1;  ///< empty: do not check
};

struct FetchSummary {
    int downloaded = 0;
    int skipped = 0;      ///< already on disk with the right hash
    QStringList failed;   ///< "target (reason)"
};

/**
 * Where a running install looks to see whether the player asked it to stop.
 *
 * One pointer for the whole process is enough: installs are one at a time, and the alternative --
 * threading a flag through every download call and every loop -- would touch every installer for a
 * button. Set it before the work starts, clear it when the work is done.
 */
void setCancellationFlag(std::atomic<bool>* flag);

/// True once the player pressed cancel; downloaders and installers check it and give up.
bool requestCancelled();

/// Downloads `url` to `target` unless the file is already there with the right hash.
FetchResult fetchFile(const QString& url, const QString& target, const QString& sha1);

/// Downloads `url` into memory; for small documents such as a version manifest.
BytesResult fetchBytes(const QString& url);

/**
 * Downloads every job, at most `concurrency` at a time, reporting how many are finished.
 *
 * Files already on disk with the right hash are skipped without a request, which is what makes an
 * interrupted install cheap to resume.
 */
FetchSummary fetchMany(const QList<FetchJob>& jobs, int concurrency, const std::function<void(int done, int total)>& onProgress = {});

struct JsonResult {
    bool ok = false;
    QString error;
    QByteArray body;
    int status = 0;
};

/// A JSON (or form) POST, for the services that want one. Headers are given as name/value pairs.
JsonResult postJson(const QString& url, const QByteArray& body, const QList<QPair<QString, QString>>& headers = {});

/// A GET with headers, for the services that want a bearer token.
JsonResult getJson(const QString& url, const QList<QPair<QString, QString>>& headers = {});

/// The sha1 of a file, lowercase hex, or empty when it cannot be read.
QString fileSha1(const QString& path);

}  // namespace Komorebi
