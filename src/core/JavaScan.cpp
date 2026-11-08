// SPDX-License-Identifier: GPL-3.0-only
#include "core/JavaScan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

#include "core/Paths.h"

namespace Komorebi {

namespace {

/// The major version people mean: 1.8.0_202 is 8, 17.0.9 is 17.
int majorOf(const QString& version)
{
    if (version.startsWith(QLatin1String("1."))) {
        bool ok = false;
        const int major = version.section(QLatin1Char('.'), 1, 1).toInt(&ok);
        return ok ? major : 0;
    }
    bool ok = false;
    const int major = version.section(QLatin1Char('.'), 0, 0).toInt(&ok);
    return ok ? major : 0;
}

QString propertyOf(const QStringList& lines, const QString& key)
{
    for (const auto& line : lines) {
        if (line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1).remove(QLatin1Char('"')).trimmed();
    }
    return {};
}

}  // namespace

JavaRuntime JavaScan::inspect(const QString& executable)
{
    JavaRuntime runtime;
    if (executable.isEmpty() || !QFileInfo::exists(executable))
        return runtime;

    runtime.path = QDir::cleanPath(executable);
    const QDir bin = QFileInfo(executable).absoluteDir();
    runtime.home = bin.absolutePath();
    runtime.managed = runtime.home.startsWith(QDir(dataRoot()).filePath(QStringLiteral("java")));

    QFile release(QDir(bin.absolutePath()).filePath(QStringLiteral("release")));
    if (release.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(release.readAll()).split(QLatin1Char('\n'));
        runtime.version = propertyOf(lines, QStringLiteral("JAVA_VERSION"));
        runtime.vendor = propertyOf(lines, QStringLiteral("IMPLEMENTOR"));
        if (runtime.vendor.isEmpty())
            runtime.vendor = propertyOf(lines, QStringLiteral("JAVA_VENDOR"));
        runtime.arch = propertyOf(lines, QStringLiteral("OS_ARCH"));
    }

    if (runtime.version.isEmpty()) {
        // No release file (Java 8 and older): ask the binary, which costs a process but happens once.
        QProcess process;
        process.start(runtime.path, { QStringLiteral("-version") });
        if (process.waitForFinished(5000)) {
            const QString output = QString::fromLocal8Bit(process.readAllStandardError());
            const QRegularExpression pattern(QStringLiteral("version \"([^\"]+)\""));
            const auto match = pattern.match(output);
            if (match.hasMatch())
                runtime.version = match.captured(1);
        }
    }

    runtime.major = majorOf(runtime.version);
    return runtime;
}

QList<JavaRuntime> JavaScan::find()
{
    QList<JavaRuntime> runtimes;
    const QString executable = QStringLiteral("java.exe");

    QStringList roots;
    // the launcher's own runtimes first: they are the ones it can vouch for
    roots << QDir(dataRoot()).filePath(QStringLiteral("java"));

    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : { QStringLiteral("PROGRAMFILES"), QStringLiteral("PROGRAMFILES(X86)"),
                             QStringLiteral("LOCALAPPDATA"), QStringLiteral("PROGRAMDATA") }) {
        const QString base = environment.value(key);
        if (base.isEmpty())
            continue;
        roots << QDir(base).filePath(QStringLiteral("Java"))
              << QDir(base).filePath(QStringLiteral("Eclipse Adoptium"))
              << QDir(base).filePath(QStringLiteral("Zulu"))
              << QDir(base).filePath(QStringLiteral("Microsoft"));
    }
    const QString javaHome = environment.value(QStringLiteral("JAVA_HOME"));
    if (!javaHome.isEmpty())
        roots << javaHome;

    QSet<QString> seen;
    for (const auto& root : roots) {
        const QDir directory(root);
        if (!directory.exists())
            continue;

        // either <root>/bin/java.exe (a JAVA_HOME) or <root>/<runtime>/bin/java.exe
        QStringList candidates;
        candidates << directory.filePath(QStringLiteral("bin/") + executable);
        for (const auto& entry : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            candidates << directory.filePath(entry + QStringLiteral("/bin/") + executable);
            // some layouts add one more level (jdk-21.0.1+12)
            const QDir inner(directory.filePath(entry));
            for (const auto& nested : inner.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                candidates << inner.filePath(nested + QStringLiteral("/bin/") + executable);
        }

        for (const auto& candidate : candidates) {
            if (!QFileInfo::exists(candidate))
                continue;
            const QString canonical = QDir::cleanPath(candidate);
            if (seen.contains(canonical))
                continue;
            seen.insert(canonical);

            const JavaRuntime runtime = inspect(canonical);
            if (runtime.major > 0)
                runtimes.append(runtime);
        }
    }

    // also whatever `java` on the PATH resolves to
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!onPath.isEmpty()) {
        const QString canonical = QDir::cleanPath(onPath);
        if (!seen.contains(canonical)) {
            const JavaRuntime runtime = inspect(canonical);
            if (runtime.major > 0)
                runtimes.append(runtime);
        }
    }

    std::sort(runtimes.begin(), runtimes.end(), [](const JavaRuntime& a, const JavaRuntime& b) {
        if (a.managed != b.managed)
            return a.managed;
        return a.major > b.major;
    });
    return runtimes;
}

JavaRuntime JavaScan::pick(const QList<JavaRuntime>& runtimes, int requiredMajor)
{
    JavaRuntime best;
    int bestDistance = std::numeric_limits<int>::max();
    for (const auto& runtime : runtimes) {
        const int distance = requiredMajor <= 0 ? 0 : std::abs(runtime.major - requiredMajor);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = runtime;
            if (distance == 0)
                break;
        }
    }
    return best;
}

}  // namespace Komorebi
