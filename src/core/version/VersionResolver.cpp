// SPDX-License-Identifier: GPL-3.0-only
#include "core/version/VersionResolver.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "core/Paths.h"

namespace Komorebi {

namespace {

QString parentOf(const QJsonObject& root)
{
    return root.value(QStringLiteral("inheritsFrom")).toString();
}

/// Child first, then whatever the parent adds that the child did not already name.
QList<Library> mergeLibraries(const QList<Library>& child, const QList<Library>& parent)
{
    QList<Library> merged = child;
    QSet<QString> seen;
    for (const auto& library : child)
        seen.insert(library.name);

    for (const auto& library : parent) {
        if (seen.contains(library.name))
            continue;
        merged.append(library);
        seen.insert(library.name);
    }
    return merged;
}

}  // namespace

ResolvedVersion resolveVersion(const QString& versionsRoot, const QString& id)
{
    ResolvedVersion result;

    QString current = id;
    QList<QPair<QString, QString>> chain;  // id, parent id, in reading order
    QSet<QString> visited;

    // Walk up to the version that inherits from nothing. The limit is a guard against a file that
    // points at itself.
    for (int depth = 0; depth < 8; ++depth) {
        if (!isSafeComponent(current)) {
            result.error = QStringLiteral("%1 is not a version id").arg(current);
            return result;
        }
        if (visited.contains(current)) {
            result.error = QStringLiteral("inheritance loops at %1").arg(current);
            return result;
        }
        visited.insert(current);

        const QString path = QDir(versionsRoot).filePath(current + QStringLiteral("/") + current + QStringLiteral(".json"));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("%1 is not installed").arg(current);
            return result;
        }
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        const QString parent = parentOf(root);
        chain.append({ current, parent });
        result.chain.append(current);
        if (parent.isEmpty())
            break;
        current = parent;
    }

    // Merge from the bottom up, so each step only has to know about one parent.
    VersionJson merged;
    for (int i = chain.size() - 1; i >= 0; --i) {
        const QString stepId = chain.at(i).first;
        const QString path = QDir(versionsRoot).filePath(stepId + QStringLiteral("/") + stepId + QStringLiteral(".json"));
        const VersionJson step = VersionJson::fromFile(path);
        if (step.id.isEmpty()) {
            result.error = QStringLiteral("%1 has a description that cannot be read").arg(stepId);
            return result;
        }

        if (merged.id.isEmpty()) {
            // The oldest version in the chain owns the game itself: jar, asset index, Java.
            merged = step;
            merged.id = id;
            continue;
        }

        merged.libraries = mergeLibraries(step.libraries, merged.libraries);
        if (!step.mainClass.isEmpty())
            merged.mainClass = step.mainClass;
        if (!step.assets.isEmpty())
            merged.assets = step.assets;
        if (step.javaMajor > merged.javaMajor)
            merged.javaMajor = step.javaMajor;
        if (!step.clientJar.url.isEmpty())
            merged.clientJar = step.clientJar;
        if (!step.assetIndex.url.isEmpty())
            merged.assetIndex = step.assetIndex;

        // The parent's arguments come first, then whatever the loader adds.
        if (!step.legacyArguments.isEmpty() && merged.legacyArguments.isEmpty()) {
            merged.legacyArguments = step.legacyArguments;
        } else if (merged.legacyArguments.isEmpty()) {
            merged.gameArguments = step.gameArguments + merged.gameArguments;
        } else {
            for (const auto& argument : step.gameArguments)
                merged.legacyArguments.append(argument.value);
        }
        merged.jvmArguments = step.jvmArguments + merged.jvmArguments;
        merged.mavenFiles += step.mavenFiles;
    }

    if (merged.id.isEmpty()) {
        result.error = QStringLiteral("nothing to resolve for %1").arg(id);
        return result;
    }

    merged.id = id;
    result.version = merged;
    // The bottom of the chain is the vanilla version: its jar and its assets are the ones to use.
    result.baseId = chain.isEmpty() ? id : chain.last().first;
    return result;
}

}  // namespace Komorebi
