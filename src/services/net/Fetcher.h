// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Downloads. One request at a time is easy to write and slow to use: every file opened a new
 *  connection, and a version has thousands of files. This keeps one connection pool and runs a
 *  few requests at once, which is what makes an install take minutes instead of half an hour.
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

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

/// The sha1 of a file, lowercase hex, or empty when it cannot be read.
QString fileSha1(const QString& path);

}  // namespace Komorebi
