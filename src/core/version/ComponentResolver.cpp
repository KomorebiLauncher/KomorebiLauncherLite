// SPDX-License-Identifier: GPL-3.0-only
#include "core/version/ComponentResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/Paths.h"

namespace Komorebi {

namespace {

/// `<metaRoot>/<uid>/<version>.json`, or nothing when the names would leave the directory.
QString componentPath(const QString& metaRoot, const ComponentRef& component)
{
    if (!isSafeComponent(component.uid) || !isSafeComponent(component.version))
        return {};
    return safeJoin(metaRoot, QStringLiteral("%1/%2.json").arg(component.uid, component.version));
}

}  // namespace
QList<ComponentRef> readComponentList(const QString& instanceDirectory)
{
    QList<ComponentRef> components;
    QFile file(QDir(instanceDirectory).filePath(QStringLiteral("mmc-pack.json")));
    if (!file.open(QIODevice::ReadOnly))
        return components;

    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& entry : root.value(QStringLiteral("components")).toArray()) {
        const auto object = entry.toObject();
        ComponentRef ref;
        ref.uid = object.value(QStringLiteral("uid")).toString();
        ref.version = object.value(QStringLiteral("version")).toString();
        if (!ref.uid.isEmpty() && !ref.version.isEmpty())
            components.append(ref);
    }
    return components;
}

VersionJson readComponent(const QString& metaRoot, const ComponentRef& component)
{
    return VersionJson::fromFile(componentPath(metaRoot, component));
}

QStringList missingComponents(const QString& metaRoot, const QList<ComponentRef>& components)
{
    QStringList missing;
    for (const auto& component : components) {
        const QString path = componentPath(metaRoot, component);
        if (path.isEmpty() || !QFileInfo::exists(path))
            missing << QStringLiteral("%1 %2").arg(component.uid, component.version);
    }
    return missing;
}

namespace {

/// The number a component uses to say who comes first; components without one sit above the game.
int orderOf(const QJsonObject& root)
{
    return root.contains(QStringLiteral("order")) ? root.value(QStringLiteral("order")).toInt() : 0;
}

/// Merges one component into the description being built.
void merge(VersionJson& target, const VersionJson& source)
{
    for (const auto& library : source.libraries) {
        // Later components supersede earlier ones with the same coordinates: a loader pins the
        // version of a library it was tested with, and that pin has to win.
        bool replaced = false;
        for (int index = 0; index < target.libraries.size(); ++index) {
            if (target.libraries[index].name == library.name) {
                target.libraries[index] = library;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            target.libraries.append(library);
    }

    target.mavenFiles += source.mavenFiles;

    if (!source.mainClass.isEmpty())
        target.mainClass = source.mainClass;
    if (!source.assets.isEmpty())
        target.assets = source.assets;
    if (source.javaMajor > target.javaMajor)
        target.javaMajor = source.javaMajor;

    // Arguments accumulate, except the old single-string form: a component that brings its own
    // replaces the inherited one, because there can only be one.
    if (!source.legacyArguments.isEmpty()) {
        target.legacyArguments = source.legacyArguments;
        target.gameArguments.clear();
    } else if (target.legacyArguments.isEmpty()) {
        target.gameArguments += source.gameArguments;
    }
    target.jvmArguments += source.jvmArguments;

    if (!source.clientJar.url.isEmpty())
        target.clientJar = source.clientJar;
    if (!source.assetIndex.url.isEmpty())
        target.assetIndex = source.assetIndex;
    // The id stays with the base component: it names the version the instance is, and the client
    // jar is looked up by it. A loader calling itself 47.4.0 is not a version of Minecraft.
    if (target.id.isEmpty() && !source.id.isEmpty())
        target.id = source.id;
}

}  // namespace

VersionJson mergeComponents(const QString& metaRoot, const QList<ComponentRef>& components)
{
    struct Loaded {
        int order = 0;
        int index = 0;
        VersionJson version;
    };

    QList<Loaded> loaded;
    int index = 0;
    for (const auto& component : components) {
        const QString path = componentPath(metaRoot, component);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            ++index;
            continue;
        }
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        loaded.append({ orderOf(root), index++, VersionJson::fromJson(QJsonDocument(root).toJson()) });
    }

    std::sort(loaded.begin(), loaded.end(), [](const Loaded& a, const Loaded& b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.index < b.index;
    });

    VersionJson merged;
    for (int i = 0; i < loaded.size(); ++i)
        merge(merged, loaded[i].version);

    if (merged.id.isEmpty() && !loaded.isEmpty())
        merged.id = loaded.last().version.id;
    return merged;
}

}  // namespace Komorebi
