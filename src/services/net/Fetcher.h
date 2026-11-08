// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The smallest useful downloader: one file, checked against its hash, written where the caller
 *  asked. Everything the launcher needs to launch (client jar, asset index) starts here.
 */

#pragma once

#include <QByteArray>
#include <QString>

namespace Komorebi {

struct FetchResult {
    bool ok = false;
    bool alreadyPresent = false;
    QString error;
    qint64 bytes = 0;
};

/// Downloads `url` to `target` unless the file is already there with the right hash.
FetchResult fetchFile(const QString& url, const QString& target, const QString& sha1);

struct BytesResult {
    bool ok = false;
    QString error;
    QByteArray data;
};

/// Downloads `url` into memory; for small documents such as a version manifest.
BytesResult fetchBytes(const QString& url);

/// The sha1 of a file, lowercase hex, or empty when it cannot be read.
QString fileSha1(const QString& path);

}  // namespace Komorebi
