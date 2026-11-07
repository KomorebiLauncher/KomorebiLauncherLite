// SPDX-License-Identifier: GPL-3.0-only
#include "core/Instance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace Komorebi {

QString loaderName(Loader loader)
{
    switch (loader) {
        case Loader::Vanilla:
            return QStringLiteral("Vanilla");
        case Loader::Forge:
            return QStringLiteral("Forge");
        case Loader::NeoForge:
            return QStringLiteral("NeoForge");
        case Loader::Fabric:
            return QStringLiteral("Fabric");
        case Loader::Quilt:
            return QStringLiteral("Quilt");
        case Loader::Unknown:
            break;
    }
    return QStringLiteral("Unknown");
}

QString Instance::summary() const
{
    QStringList parts;
    if (!minecraftVersion.isEmpty())
        parts << minecraftVersion;
    if (loader != Loader::Vanilla && loader != Loader::Unknown) {
        QString entry = loaderName(loader);
        if (!loaderVersion.isEmpty())
            entry += QLatin1Char(' ') + loaderVersion;
        parts << entry;
    }
    return parts.join(QStringLiteral(" · "));
}

InstanceStore::InstanceStore(QString root) : m_root(std::move(root)) {}

namespace {

/// instance.cfg is an INI file; QSettings reads it without us inventing a parser.
void readConfig(const QString& directory, Instance& instance)
{
    const QString path = QDir(directory).filePath(QStringLiteral("instance.cfg"));
    if (!QFileInfo::exists(path))
        return;

    QSettings config(path, QSettings::IniFormat);
    config.beginGroup(QStringLiteral("General"));
    instance.name = config.value(QStringLiteral("name")).toString();
    instance.iconKey = config.value(QStringLiteral("iconKey")).toString();
    config.endGroup();

    if (instance.name.isEmpty())
        instance.name = instance.id;
    if (instance.iconKey.isEmpty())
        instance.iconKey = QStringLiteral("default");
}

/// mmc-pack.json lists the components; the Minecraft and loader entries are what a card shows.
void readComponents(const QString& directory, Instance& instance)
{
    QFile file(QDir(directory).filePath(QStringLiteral("mmc-pack.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;

    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const struct {
        const char* uid;
        Loader loader;
    } known[] = {
        { "net.minecraft", Loader::Vanilla },
        { "net.minecraftforge", Loader::Forge },
        { "net.neoforged", Loader::NeoForge },
        { "net.fabricmc.fabric-loader", Loader::Fabric },
        { "org.quiltmc.quilt-loader", Loader::Quilt },
    };

    for (const auto& component : root.value(QStringLiteral("components")).toArray()) {
        const auto object = component.toObject();
        const QString uid = object.value(QStringLiteral("uid")).toString();
        const QString version = object.value(QStringLiteral("version")).toString();
        for (const auto& entry : known) {
            if (uid != QLatin1String(entry.uid))
                continue;
            if (entry.loader == Loader::Vanilla) {
                instance.minecraftVersion = version;
            } else {
                instance.loader = entry.loader;
                instance.loaderVersion = version;
            }
        }
    }

    // A pack with no Minecraft component cannot be launched, whatever else it has.
    instance.playable = !instance.minecraftVersion.isEmpty();
}

}  // namespace

QList<Instance> InstanceStore::scan() const
{
    QList<Instance> instances;

    QDir root(m_root);
    if (!root.exists())
        return instances;

    const auto entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& entry : entries) {
        Instance instance;
        instance.directory = entry.absoluteFilePath();
        instance.id = entry.fileName();

        readConfig(instance.directory, instance);
        readComponents(instance.directory, instance);

        // Anything without the config is not an instance: it is a folder someone dropped here.
        if (!QFileInfo::exists(QDir(instance.directory).filePath(QStringLiteral("instance.cfg"))))
            continue;

        instances.append(instance);
    }
    return instances;
}

}  // namespace Komorebi
