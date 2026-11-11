// SPDX-License-Identifier: GPL-3.0-only
#include "core/install/ManualMods.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/Paths.h"

namespace Komorebi {

namespace {

constexpr auto kManualFile = "manual-mods.json";

QString listPath(const QString& versionDirectory)
{
    return QDir(versionDirectory).filePath(QString::fromLatin1(kManualFile));
}

/// Lowercase hex of a file's sha1, empty when it cannot be read.
QString sha1Of(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

/// Moves a file into place, falling back to copy-and-remove for when a rename cannot cross volumes.
bool moveFile(const QString& from, const QString& to)
{
    if (QFile::rename(from, to))
        return true;
    if (!QFile::copy(from, to))
        return false;
    QFile::remove(from);
    return true;
}

/// Whether the file at `path` is the file the list asked for. A listing with no hash can only be
/// matched by name, and the caller has already done that.
bool holdsWantedFile(const ManualMod& wanted, const QString& path)
{
    if (wanted.sha1.isEmpty())
        return true;
    return sha1Of(path).compare(wanted.sha1, Qt::CaseInsensitive) == 0;
}

}  // namespace

QList<ManualMod> readManualMods(const QString& versionDirectory)
{
    QList<ManualMod> mods;

    QFile file(listPath(versionDirectory));
    if (!file.open(QIODevice::ReadOnly))
        return mods;

    for (const auto& entry : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("mods")).toArray()) {
        const auto item = entry.toObject();
        ManualMod mod;
        mod.name = item.value(QStringLiteral("name")).toString();
        mod.version = item.value(QStringLiteral("version")).toString();
        mod.fileName = item.value(QStringLiteral("fileName")).toString();
        mod.sha1 = item.value(QStringLiteral("sha1")).toString();
        mod.pageUrl = item.value(QStringLiteral("pageUrl")).toString();
        mod.projectId = item.value(QStringLiteral("projectId")).toInt();
        mod.fileId = item.value(QStringLiteral("fileId")).toInt();
        // The file name is how hokora is searched, and how the list is shown; an entry without one
        // would make the count say a mod is missing that was never written down.
        if (!mod.fileName.isEmpty())
            mods.append(mod);
    }
    return mods;
}

bool writeManualMods(const QString& versionDirectory, const QList<ManualMod>& mods)
{
    const QString path = listPath(versionDirectory);
    if (mods.isEmpty()) {
        QFile::remove(path);
        return true;
    }

    QJsonArray array;
    for (const auto& mod : mods) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), mod.name);
        object.insert(QStringLiteral("version"), mod.version);
        object.insert(QStringLiteral("fileName"), mod.fileName);
        object.insert(QStringLiteral("sha1"), mod.sha1);
        object.insert(QStringLiteral("pageUrl"), mod.pageUrl);
        object.insert(QStringLiteral("projectId"), mod.projectId);
        object.insert(QStringLiteral("fileId"), mod.fileId);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("mods"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

HokoraHarvest harvestHokora(const QString& versionDirectory)
{
    HokoraHarvest harvest;
    const QList<ManualMod> pending = readManualMods(versionDirectory);
    if (pending.isEmpty())
        return harvest;

    const QDir hokora(hokoraDirectory());
    if (!hokora.exists())
        return harvest;

    // Hokora is a folder a person drops files into, so only plain files at the top count: a folder
    // named like a jar is not a jar, and looking inside it would be guessing.
    QList<QFileInfo> available = hokora.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    const QString modsDirectory = QDir(versionDirectory).filePath(QStringLiteral("mods"));
    QDir().mkpath(modsDirectory);

    QList<ManualMod> left;
    for (const auto& wanted : pending) {
        int match = -1;
        for (int i = 0; i < available.size() && match < 0; ++i) {
            const QFileInfo& candidate = available.at(i);
            const bool byName = wanted.sha1.isEmpty()
                                && candidate.fileName().compare(wanted.fileName, Qt::CaseInsensitive) == 0;
            const bool byHash = !wanted.sha1.isEmpty()
                                && sha1Of(candidate.absoluteFilePath()).compare(wanted.sha1, Qt::CaseInsensitive) == 0;
            if (byName || byHash)
                match = i;
        }

        if (match < 0) {
            // A file carrying the wanted name but other bytes is the mistake worth reporting: it is
            // the one a person makes by grabbing the newest version instead of the one asked for.
            if (!wanted.sha1.isEmpty()) {
                for (const auto& candidate : available) {
                    if (candidate.fileName().compare(wanted.fileName, Qt::CaseInsensitive) == 0) {
                        if (!harvest.rejected.contains(wanted.fileName))
                            harvest.rejected << wanted.fileName;
                        break;
                    }
                }
            }
            left.append(wanted);
            continue;
        }

        const QFileInfo source = available.at(match);
        // The name the pack asked for, not the name the browser saved: a download that arrived as
        // "jei (1).jar" still belongs in the pack under the name its manifest lists, and a reinstall
        // looks for exactly that name to decide the file is already there.
        const QString name = QFileInfo(wanted.fileName).fileName();
        const QString target = QDir(modsDirectory).filePath(name);
        if (QFileInfo::exists(target)) {
            // Something already sits where the file should go. When it is the wanted file the pack
            // is complete; when it is not, moving over it would replace a mod without saying so.
            if (!holdsWantedFile(wanted, target)) {
                if (!harvest.rejected.contains(source.fileName()))
                    harvest.rejected << source.fileName();
                left.append(wanted);
                continue;
            }
            available.removeAt(match);
            ++harvest.taken;
            continue;
        }

        if (!moveFile(source.absoluteFilePath(), target)) {
            left.append(wanted);
            continue;
        }
        available.removeAt(match);
        ++harvest.taken;
    }

    writeManualMods(versionDirectory, left);
    return harvest;
}

}  // namespace Komorebi
