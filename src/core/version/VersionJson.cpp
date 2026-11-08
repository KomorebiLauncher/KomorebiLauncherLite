// SPDX-License-Identifier: GPL-3.0-only
#include "core/version/VersionJson.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace Komorebi {

Environment Environment::current()
{
    Environment environment;
#if defined(Q_OS_WIN)
    environment.osName = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    environment.osName = QStringLiteral("darwin");
#else
    environment.osName = QStringLiteral("linux");
#endif
    environment.osArch = QSysInfo::currentCpuArchitecture();
    environment.osVersion = QSysInfo::productVersion();
    return environment;
}

QString classpathSeparator(const Environment& environment)
{
    return environment.osName == QLatin1String("windows") ? QStringLiteral(";") : QStringLiteral(":");
}

QString javaExecutableName(const Environment& environment)
{
    return environment.osName == QLatin1String("windows") ? QStringLiteral("java.exe") : QStringLiteral("java");
}

bool Rule::matches(const Environment& environment) const
{
    // A rule about optional features never matches: this launcher asks for none of them.
    if (hasFeatures)
        return false;

    if (!osName.isEmpty() && osName != environment.osName)
        return false;
    if (!osArch.isEmpty()) {
        // The manifests say "x86" where Qt says "i386".
        const bool archMatches = osArch == environment.osArch ||
                                 (osArch == QLatin1String("x86") && environment.osArch.startsWith(QLatin1String("i386")));
        if (!archMatches)
            return false;
    }
    if (!osVersion.isEmpty()) {
        const QRegularExpression pattern(QRegularExpression::anchoredPattern(osVersion));
        if (!pattern.match(environment.osVersion).hasMatch())
            return false;
    }
    return true;
}

bool rulesAllow(const QList<Rule>& rules, const Environment& environment)
{
    // No rules at all means "for everyone". Once there are rules, the answer starts at "no" and
    // only a matching allow can turn it on -- otherwise a library kept for osx would be downloaded
    // on Windows, which is exactly the bug this comment exists to remember.
    bool allowed = rules.isEmpty();
    for (const auto& rule : rules) {
        if (!rule.matches(environment))
            continue;
        allowed = rule.action == Rule::Action::Allow;
    }
    return allowed;
}

namespace {

QList<Rule> readRules(const QJsonValue& value)
{
    QList<Rule> rules;
    for (const auto& entry : value.toArray()) {
        const auto object = entry.toObject();
        Rule rule;
        rule.action = object.value(QStringLiteral("action")).toString() == QLatin1String("allow") ? Rule::Action::Allow
                                                                                                 : Rule::Action::Disallow;
        const auto os = object.value(QStringLiteral("os")).toObject();
        rule.osName = os.value(QStringLiteral("name")).toString();
        rule.osArch = os.value(QStringLiteral("arch")).toString();
        rule.osVersion = os.value(QStringLiteral("version")).toString();
        rule.hasFeatures = object.contains(QStringLiteral("features"));
        rules.append(rule);
    }
    return rules;
}

QList<Argument> readArguments(const QJsonArray& array)
{
    QList<Argument> arguments;
    for (const auto& entry : array) {
        if (entry.isString()) {
            arguments.append({ entry.toString(), {} });
            continue;
        }
        const auto object = entry.toObject();
        Argument argument;
        argument.rules = readRules(object.value(QStringLiteral("rules")));
        const auto value = object.value(QStringLiteral("value"));
        if (value.isString()) {
            argument.value = value.toString();
            arguments.append(argument);
        } else {
            // A single entry may stand for several values; they share the rules.
            for (const auto& item : value.toArray()) {
                Argument one = argument;
                one.value = item.toString();
                arguments.append(one);
            }
        }
    }
    return arguments;
}

Download readDownload(const QJsonObject& object)
{
    Download download;
    download.path = object.value(QStringLiteral("path")).toString();
    download.url = object.value(QStringLiteral("url")).toString();
    download.sha1 = object.value(QStringLiteral("sha1")).toString();
    download.size = qint64(object.value(QStringLiteral("size")).toDouble());
    return download;
}

/// The official manifests give a download without a path: it is the maven coordinates, written out.
QString pathFromCoordinates(const QString& name)
{
    const QStringList parts = name.split(QLatin1Char(':'));
    if (parts.size() < 3)
        return {};
    const QString group = QString(parts.at(0)).replace(QLatin1Char('.'), QLatin1Char('/'));
    const QString artifact = parts.at(1);
    const QString version = parts.at(2);
    const QString classifier = parts.size() > 3 ? QStringLiteral("-") + parts.at(3) : QString();
    return QStringLiteral("%1/%2/%3/%2-%3%4.jar").arg(group, artifact, version, classifier);
}
QString nativeClassifierFor(const QJsonObject& natives, const Environment& environment)
{
    // The key is the platform; the value is the classifier with ${arch} already substituted by
    // whoever wrote the manifest -- we only have to pick the right key.
    QString key = environment.osName;
    QString classifier = natives.value(key).toString();
    if (classifier.isEmpty() && environment.osName == QLatin1String("windows") && environment.osArch.startsWith(QLatin1String("x86_64")))
        classifier = natives.value(QStringLiteral("windows-64")).toString();
    return classifier;
}

}  // namespace

VersionJson VersionJson::fromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    VersionJson version = fromJson(file.readAll());
    if (version.id.isEmpty())
        version.id = QFileInfo(path).completeBaseName();
    return version;
}

VersionJson VersionJson::fromJson(const QByteArray& json)
{
    VersionJson version;
    const auto root = QJsonDocument::fromJson(json).object();
    version.id = root.value(QStringLiteral("id")).toString();
    if (version.id.isEmpty())
        version.id = root.value(QStringLiteral("version")).toString();
    version.mainClass = root.value(QStringLiteral("mainClass")).toString();
    version.assets = root.value(QStringLiteral("assets")).toString();
    if (version.assets.isEmpty())
        version.assets = root.value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString();
    version.javaMajor = root.value(QStringLiteral("javaVersion")).toObject().value(QStringLiteral("majorVersion")).toInt(0);
    if (version.javaMajor == 0)
        version.javaMajor = root.value(QStringLiteral("compatibleJavaMajors")).toArray().first().toInt(8);

    const Environment environment = Environment::current();

    for (const auto& entry : root.value(QStringLiteral("libraries")).toArray()) {
        const auto object = entry.toObject();
        Library library;
        library.name = object.value(QStringLiteral("name")).toString();

        const auto rules = readRules(object.value(QStringLiteral("rules")));
        if (!rulesAllow(rules, environment))
            continue;

        library.artifact = readDownload(object.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("artifact")).toObject());
        if (library.artifact.path.isEmpty())
            library.artifact.path = pathFromCoordinates(library.name);

        // Fabric and Quilt describe their libraries the maven way: a coordinate, a repository base
        // and the hash, with no "downloads" block at all. Without this they resolve to files with
        // no url, and an install fails on every one of them.
        if (library.artifact.url.isEmpty()) {
            const QString repository = object.value(QStringLiteral("url")).toString();
            if (!repository.isEmpty() && !library.artifact.path.isEmpty())
                library.artifact.url = repository.endsWith(QLatin1Char('/')) ? repository + library.artifact.path
                                                                             : repository + QLatin1Char('/') + library.artifact.path;
            if (library.artifact.sha1.isEmpty())
                library.artifact.sha1 = object.value(QStringLiteral("sha1")).toString();
        }

        const auto natives = object.value(QStringLiteral("natives")).toObject();
        if (!natives.isEmpty()) {
            const QString classifier = nativeClassifierFor(natives, environment);
            if (!classifier.isEmpty()) {
                library.hasNatives = true;
                // Prism's manifest writes the classifier; the official one writes a placeholder that
                // has to be substituted with the actual one.
                QString key = classifier;
                const QString arch = environment.osArch.startsWith(QLatin1String("x86_64")) ? QStringLiteral("64") : QStringLiteral("32");
                key.replace(QStringLiteral("${arch}"), arch);
                library.natives = readDownload(object.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("classifiers")).toObject().value(key).toObject());
            }
        }

        version.libraries.append(library);
    }

    // The game itself: the official manifest calls it downloads.client, the component format keeps
    // it under mainJar.
    // "where is the game" is a question with three answers in the wild; the one thing that makes a
    // download usable is its url, because the file name is ours to choose.

    version.clientJar = readDownload(root.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("client")).toObject());
    if (version.clientJar.url.isEmpty())
        version.clientJar = readDownload(root.value(QStringLiteral("mainJar")).toObject().value(QStringLiteral("downloads")).toObject().value(QStringLiteral("artifact")).toObject());
    if (version.clientJar.url.isEmpty())
        version.clientJar = readDownload(root.value(QStringLiteral("mainJar")).toObject());
    if (version.clientJar.path.isEmpty() && !version.clientJar.url.isEmpty())
        version.clientJar.path = QStringLiteral("net/minecraft/client/%1/client-%1.jar").arg(version.id);

    const auto index = root.value(QStringLiteral("assetIndex")).toObject();
    version.assetIndex = readDownload(index);
    version.assetIndex.path = QStringLiteral("indexes/%1.json").arg(index.value(QStringLiteral("id")).toString());

    // mavenFiles: the same shape as libraries, but they belong to an installer rather than to the
    // game. They still have to be on the classpath for the loader to run its processors.
    for (const auto& entry : root.value(QStringLiteral("mavenFiles")).toArray()) {
        const auto object = entry.toObject();
        Library file;
        file.name = object.value(QStringLiteral("name")).toString();
        file.artifact = readDownload(object.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("artifact")).toObject());
        if (file.artifact.path.isEmpty())
            file.artifact.path = pathFromCoordinates(file.name);
        if (file.artifact.valid() || !file.artifact.url.isEmpty())
            version.mavenFiles.append(file);
    }

    const auto arguments = root.value(QStringLiteral("arguments")).toObject();
    version.gameArguments = readArguments(arguments.value(QStringLiteral("game")).toArray());
    version.jvmArguments = readArguments(arguments.value(QStringLiteral("jvm")).toArray());

    if (version.jvmArguments.isEmpty()) {
        for (const auto& entry : root.value(QStringLiteral("+jvmArgs")).toArray())
            version.jvmArguments.append({ entry.toString(), {} });
    }

    const QString legacy = root.value(QStringLiteral("minecraftArguments")).toString();
    if (!legacy.isEmpty())
        version.legacyArguments = legacy.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    return version;
}

QStringList VersionJson::resolvedGameArguments(const Environment& environment) const
{
    if (!legacyArguments.isEmpty())
        return legacyArguments;

    QStringList result;
    for (const auto& argument : gameArguments) {
        if (argument.applies(environment))
            result << argument.value;
    }
    return result;
}

QStringList VersionJson::resolvedJvmArguments(const Environment& environment) const
{
    QStringList result;
    for (const auto& argument : jvmArguments) {
        if (argument.applies(environment))
            result << argument.value;
    }
    return result;
}

}  // namespace Komorebi
