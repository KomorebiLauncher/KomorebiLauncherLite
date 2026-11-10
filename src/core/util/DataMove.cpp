// SPDX-License-Identifier: GPL-3.0-only
#include "core/util/DataMove.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "core/Paths.h"

namespace Komorebi {

namespace {

/// Whether this entry is one of the two things a move never carries.
bool skips(const QString& relative)
{
    return relative == QLatin1String("tmp") || relative == QLatin1String("data-root.txt");
}

}  // namespace

DataCopyResult copyDataDirectory(const QString& from, const QString& to, const DataCopyProgress& progress)
{
    DataCopyResult result;

    const QString source = QDir::cleanPath(from);
    const QString target = QDir::cleanPath(to);
    if (source.isEmpty() || target.isEmpty()) {
        result.error = QStringLiteral("no directory to copy from or to");
        return result;
    }
    if (!QFileInfo(source).isDir()) {
        result.error = QStringLiteral("%1 is not a directory").arg(QDir::toNativeSeparators(source));
        return result;
    }
    // Copying a directory into itself would walk the files it is creating.
    if (staysInside(source, target) || staysInside(target, source)) {
        result.error = QStringLiteral("the two directories overlap");
        return result;
    }

    QDirIterator it(source, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString relative = QDir(source).relativeFilePath(filePath);
        if (skips(relative) || relative.startsWith(QLatin1String("tmp/")) || relative.startsWith(QLatin1String("tmp\\")))
            continue;

        const QString destination = QDir(target).filePath(relative);
        QDir().mkpath(QFileInfo(destination).absolutePath());
        // The same file can arrive under a different spelling of its path — a drive letter of another
        // case is enough on Windows — and then the remove below would delete the source before the
        // copy could replace it. Say so and move on; there is nothing to copy.
        const QFileInfo existing(destination);
        if (existing.exists()
            && existing.canonicalFilePath() == QFileInfo(filePath).canonicalFilePath())
            continue;
        if (QFile::exists(destination) && !QFile::remove(destination)) {
            result.error = QStringLiteral("could not replace %1").arg(QDir::toNativeSeparators(destination));
            return result;
        }
        if (!QFile::copy(filePath, destination)) {
            result.error = QStringLiteral("could not copy %1").arg(QDir::toNativeSeparators(filePath));
            return result;
        }

        result.files += 1;
        result.bytes += QFileInfo(filePath).size();
        if (progress && !progress(relative, result.files, result.bytes)) {
            result.cancelled = true;
            return result;
        }
    }

    return result;
}

}  // namespace Komorebi
