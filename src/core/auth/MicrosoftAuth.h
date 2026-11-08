// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Microsoft sign-in, the way a desktop launcher has to do it: a device code the player types into
 *  a browser, then a chain of exchanges that ends with a Minecraft access token.
 *
 *      device code → token → Xbox Live → XSTS → Minecraft services → profile
 *
 *  Every step is a plain HTTPS request, and the game only sees the last result.
 */

#pragma once

#include <QString>

#include <functional>

namespace Komorebi {

struct DeviceCode {
    QString deviceCode;      ///< the secret to poll with
    QString userCode;        ///< what the player types
    QString verificationUri; ///< where they type it
    int interval = 5;        ///< seconds between polls
    int expiresIn = 900;
};

struct MinecraftAccount {
    QString name;         ///< the player name, as the game shows it
    QString uuid;         ///< without dashes
    QString accessToken;  ///< for this session
    QString refreshToken; ///< to get a new access token next time
    QString xuid;
};

/// What one poll of the token endpoint said.
struct PollResult {
    enum class State {
        Pending,   ///< the player has not approved yet
        SlowDown,  ///< approved too slowly; poll less often
        Done,      ///< the tokens are in `account`
        Failed,    ///< give up, `error` says why
    };

    State state = State::Pending;
    QString error;
    MinecraftAccount account;
};

class MicrosoftAuth {
   public:
    /// The client id the launcher is registered under; empty means signing in is not configured.
    static QString clientId();

    /// Asks Microsoft for a code to show the player.
    static DeviceCode startDeviceCode(QString* error);

    /**
     * Waits for the player to finish in the browser, then walks the rest of the chain.
     *
     * `keepWaiting` is asked before every poll so a cancelled dialog stops the loop; `onCode` is
     * called once with what to show.
     */
    static bool signIn(const DeviceCode& code, MinecraftAccount* account, QString* error,
                       const std::function<bool()>& keepWaiting = {});

    /**
     * One attempt at the token endpoint, then the rest of the chain when it succeeds.
     *
     * Returns immediately: the caller polls on its own timer, so a window stays responsive while a
     * player is still typing the code in a browser.
     */
    static PollResult poll(const DeviceCode& code);

    /// Turns a stored refresh token into a fresh account, without the player doing anything.
    static bool refresh(MinecraftAccount* account, QString* error);
};

}  // namespace Komorebi
