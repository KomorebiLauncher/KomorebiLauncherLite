// SPDX-License-Identifier: GPL-3.0-only
#include "core/JavaScan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <iterator>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

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

/// Reads `    key = value` as printed by -XshowSettings:properties.
QString settingOf(const QStringList& lines, const QString& key)
{
    for (const auto& line : lines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        if (line.left(equals).trimmed() != key)
            continue;
        return line.mid(equals + 1).remove(QLatin1Char('"')).trimmed();
    }
    return {};
}

/// The last resort when neither the release file nor the binary names a vendor: the folder does.
QString vendorFromPath(const QString& path)
{
    struct Mark {
        const char* text;
        const char* vendor;
    };
    static const Mark marks[] = {
        { "temurin", "Eclipse Temurin" },   { "adoptium", "Eclipse Adoptium" },
        { "zulu", "Azul Zulu" },            { "corretto", "Amazon Corretto" },
        { "liberica", "BellSoft Liberica" }, { "microsoft", "Microsoft OpenJDK" },
        { "jbr", "JetBrains Runtime" },     { "oracle", "Oracle" },
    };
    for (const auto& mark : marks) {
        if (path.contains(QLatin1String(mark.text), Qt::CaseInsensitive))
            return QString::fromLatin1(mark.vendor);
    }
    return {};
}

/// `os.arch` as Windows reports it, written the way the page shows it.
QString archOf(const QString& property)
{
    const QString lower = property.toLower();
    if (lower.contains(QLatin1String("amd64")) || lower.contains(QLatin1String("x86_64"))
        || lower.contains(QLatin1String("aarch64")) || lower.contains(QLatin1String("arm64")))
        return property.toLower().contains(QLatin1String("arm")) ? QStringLiteral("aarch64") : QStringLiteral("x86_64");
    if (lower == QLatin1String("x86") || lower.contains(QLatin1String("i386")))
        return QStringLiteral("x86");
    return property;
}

/// Identifies one installation even when the same one is reachable through a junction.
QString identityOf(const QString& executable)
{
#ifdef Q_OS_WIN
    // Qt's canonical path leaves a junction in the middle of the path alone -- "latest\jdk-25" stays
    // "latest\jdk-25" -- so the same runtime was listed twice under two different names. Asking the
    // file system for the final path is the only way to see that both are one installation.
    const HANDLE handle = ::CreateFileW(reinterpret_cast<LPCWSTR>(executable.utf16()), 0,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        wchar_t buffer[1024];
        const DWORD length = ::GetFinalPathNameByHandleW(handle, buffer,
                                                         static_cast<DWORD>(std::size(buffer)),
                                                         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        ::CloseHandle(handle);
        if (length > 0 && length < static_cast<DWORD>(std::size(buffer))) {
            QString resolved = QString::fromWCharArray(buffer, static_cast<int>(length));
            if (resolved.startsWith(QLatin1String("\\\\?\\")))
                resolved = resolved.mid(4);
            return QDir::cleanPath(resolved).toLower();
        }
    }
#endif
    const QString canonical = QFileInfo(executable).canonicalFilePath();
    return (canonical.isEmpty() ? QDir::cleanPath(executable) : canonical).toLower();
}

}  // namespace

JavaRuntime JavaScan::inspect(const QString& executable)
{
    JavaRuntime runtime;
    if (executable.isEmpty() || !QFileInfo::exists(executable))
        return runtime;

    runtime.path = QDir::cleanPath(executable);
    const QDir bin = QFileInfo(executable).absoluteDir();
    // The home is the runtime's own directory: java.exe sits in <home>/bin. Reading it from `bin`
    // looked for <home>/bin/release, which never exists, so every runtime reported an unknown
    // vendor and no architecture.
    runtime.home = QFileInfo(bin.absolutePath()).absolutePath();
    runtime.managed = runtime.home.startsWith(QDir(dataRoot()).filePath(QStringLiteral("java")));

    QFile release(QDir(runtime.home).filePath(QStringLiteral("release")));
    if (release.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(release.readAll()).split(QLatin1Char('\n'));
        runtime.version = propertyOf(lines, QStringLiteral("JAVA_VERSION"));
        runtime.vendor = propertyOf(lines, QStringLiteral("IMPLEMENTOR"));
        if (runtime.vendor.isEmpty())
            runtime.vendor = propertyOf(lines, QStringLiteral("JAVA_VENDOR"));
        runtime.arch = archOf(propertyOf(lines, QStringLiteral("OS_ARCH")));
    }

    if (runtime.version.isEmpty() || runtime.vendor.isEmpty() || runtime.arch.isEmpty()) {
        // Java 8 and older ship no release file, and a repackaged build may leave fields out: ask the
        // binary itself. One process, once per runtime.
        QProcess process;
        process.start(runtime.path, { QStringLiteral("-XshowSettings:properties"), QStringLiteral("-version") });
        if (process.waitForFinished(5000)) {
            QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
            output += QLatin1Char('\n');
            output += QString::fromLocal8Bit(process.readAllStandardError());

            if (runtime.version.isEmpty()) {
                const QRegularExpression pattern(QStringLiteral("version \"([^\"]+)\""));
                const auto match = pattern.match(output);
                if (match.hasMatch())
                    runtime.version = match.captured(1);
            }

            const QStringList lines = output.split(QLatin1Char('\n'));
            if (runtime.vendor.isEmpty())
                runtime.vendor = settingOf(lines, QStringLiteral("java.vendor"));
            if (runtime.arch.isEmpty())
                runtime.arch = archOf(settingOf(lines, QStringLiteral("os.arch")));
        }
    }

    if (runtime.vendor.isEmpty())
        runtime.vendor = vendorFromPath(runtime.home);
    if (runtime.arch.isEmpty() && runtime.home.contains(QLatin1String("(x86)"), Qt::CaseInsensitive))
        runtime.arch = QStringLiteral("x86");

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
            // `latest/jdk-25` is a junction to `jdk-25.0.2`: both paths reach one installation and the
            // list showed it twice.
            const QString identity = identityOf(candidate);
            if (seen.contains(identity))
                continue;
            seen.insert(identity);

            const JavaRuntime runtime = inspect(candidate);
            if (runtime.major > 0)
                runtimes.append(runtime);
        }
    }

    // also whatever `java` on the PATH resolves to
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!onPath.isEmpty() && !seen.contains(identityOf(onPath))) {
        const JavaRuntime runtime = inspect(onPath);
        if (runtime.major > 0)
            runtimes.append(runtime);
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
