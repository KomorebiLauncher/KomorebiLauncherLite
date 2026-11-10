// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The accounts this launcher knows about.
 *
 *  A refresh token is a password: it is stored encrypted with the Windows data protection API, so
 *  another account on the same machine cannot read it, and a copied file is useless elsewhere.
 */

#pragma once

#include <QList>
#include <QString>

#include "core/auth/MicrosoftAuth.h"

namespace Komorebi {

class AccountStore {
   public:
    static QList<MinecraftAccount> load();
    static bool save(const QList<MinecraftAccount>& accounts);

    /// Adds or replaces an account by uuid.
    static bool add(const MinecraftAccount& account);

    static void remove(const QString& uuid);

    /// The account the game plays as; empty when nobody was picked, which means the first one.
    static QString activeUuid();

    /// Picks the account the game plays as; a uuid that is not in the store changes nothing.
    static bool setActive(const QString& uuid);
};

}  // namespace Komorebi
