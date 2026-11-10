// SPDX-License-Identifier: GPL-3.0-only
#include "core/auth/AccountStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#endif

#include "core/Paths.h"

namespace Komorebi {

namespace {

QString accountsPath()
{
    return QDir(dataRoot()).filePath(QStringLiteral("accounts.json"));
}

/// The refresh token, sealed to this Windows user. Empty in, empty out.
QString seal(const QString& secret)
{
#if defined(Q_OS_WIN)
    if (secret.isEmpty())
        return {};

    QByteArray plain = secret.toUtf8();
    DATA_BLOB input{ static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(plain.data()) };
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"KomorebiLite", nullptr, nullptr, nullptr, 0, &output))
        return {};

    const QByteArray sealed(reinterpret_cast<const char*>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return QString::fromLatin1(sealed.toBase64());
#else
    Q_UNUSED(secret);
    return {};
#endif
}

QString unseal(const QString& sealedBase64)
{
#if defined(Q_OS_WIN)
    if (sealedBase64.isEmpty())
        return {};

    const QByteArray sealed = QByteArray::fromBase64(sealedBase64.toLatin1());
    DATA_BLOB input{ static_cast<DWORD>(sealed.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(sealed.constData())) };
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
        return {};

    const QString secret = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData),
                                             static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return secret;
#else
    Q_UNUSED(sealedBase64);
    return {};
#endif
}

}  // namespace

/// Writes the whole file in one go, so a half-written account list can never be read back.
namespace {

bool writeRoot(const QJsonObject& root)
{
    const QString path = accountsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QJsonObject readRoot()
{
    QFile file(accountsPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

}  // namespace

QList<MinecraftAccount> AccountStore::load()
{
    QList<MinecraftAccount> accounts;

    QFile file(accountsPath());
    if (!file.open(QIODevice::ReadOnly))
        return accounts;

    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& entry : root.value(QStringLiteral("accounts")).toArray()) {
        const auto object = entry.toObject();
        MinecraftAccount account;
        account.name = object.value(QStringLiteral("name")).toString();
        account.uuid = object.value(QStringLiteral("uuid")).toString();
        account.xuid = object.value(QStringLiteral("xuid")).toString();
        account.refreshToken = unseal(object.value(QStringLiteral("refreshToken")).toString());
        if (!account.uuid.isEmpty())
            accounts.append(account);
    }
    return accounts;
}

bool AccountStore::save(const QList<MinecraftAccount>& accounts)
{
    // Which account is in use lives in the same file, so writing the list has to keep it; an account
    // that is gone cannot stay the choice.
    QString active = activeUuid();
    bool stillThere = false;
    for (const auto& account : accounts) {
        if (account.uuid == active) {
            stillThere = true;
            break;
        }
    }
    if (!stillThere)
        active.clear();

    QJsonArray list;
    for (const auto& account : accounts) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), account.name);
        object.insert(QStringLiteral("uuid"), account.uuid);
        object.insert(QStringLiteral("xuid"), account.xuid);
        // Only the refresh token is kept: an access token expires in a day and is asked for again.
        object.insert(QStringLiteral("refreshToken"), seal(account.refreshToken));
        list.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("accounts"), list);
    root.insert(QStringLiteral("active"), active);

    return writeRoot(root);
}

QString AccountStore::activeUuid()
{
    return readRoot().value(QStringLiteral("active")).toString();
}

bool AccountStore::setActive(const QString& uuid)
{
    QJsonObject root = readRoot();
    bool found = false;
    for (const auto& entry : root.value(QStringLiteral("accounts")).toArray()) {
        if (entry.toObject().value(QStringLiteral("uuid")).toString() == uuid) {
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    root.insert(QStringLiteral("active"), uuid);
    return writeRoot(root);
}

bool AccountStore::add(const MinecraftAccount& account)
{
    QList<MinecraftAccount> accounts = load();
    for (auto& existing : accounts) {
        if (existing.uuid == account.uuid) {
            existing = account;
            return save(accounts);
        }
    }
    accounts.append(account);
    return save(accounts);
}

void AccountStore::remove(const QString& uuid)
{
    QList<MinecraftAccount> accounts = load();
    for (int i = accounts.size() - 1; i >= 0; --i) {
        if (accounts.at(i).uuid == uuid)
            accounts.removeAt(i);
    }
    save(accounts);
}

}  // namespace Komorebi
