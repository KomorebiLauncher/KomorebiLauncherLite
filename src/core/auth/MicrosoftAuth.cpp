// SPDX-License-Identifier: GPL-3.0-only
#include "core/auth/MicrosoftAuth.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include "core/Paths.h"
#include "services/net/Fetcher.h"

namespace Komorebi {

namespace {

constexpr auto kDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr auto kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
constexpr auto kXboxUserUrl = "https://user.auth.xboxlive.com/user/authenticate";
constexpr auto kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr auto kMinecraftLoginUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr auto kMinecraftProfileUrl = "https://api.minecraftservices.com/minecraft/profile";
constexpr auto kScope = "XboxLive.signin offline_access";

QByteArray form(const QList<QPair<QString, QString>>& fields)
{
    QUrlQuery query;
    for (const auto& field : fields)
        query.addQueryItem(field.first, field.second);
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

QJsonObject parse(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

/// Turns the Xbox Live exchanges into a Minecraft access token, then asks who the player is.
bool finishSignIn(const QString& xboxToken, const QString& userHash, MinecraftAccount* account, QString* error)
{
    const QJsonObject login{
        { QStringLiteral("identityToken"), QStringLiteral("XBL3.0 x=%1;%2").arg(userHash, xboxToken) },
    };
    const JsonResult loginResult = postJson(QString::fromLatin1(kMinecraftLoginUrl),
                                            QJsonDocument(login).toJson(QJsonDocument::Compact));
    const QJsonObject loginBody = parse(loginResult.body);
    if (!loginResult.ok) {
        *error = loginBody.value(QStringLiteral("errorMessage")).toString(loginResult.error);
        if (error->isEmpty())
            *error = QStringLiteral("Minecraft services refused the sign-in");
        return false;
    }

    account->accessToken = loginBody.value(QStringLiteral("access_token")).toString();
    if (account->accessToken.isEmpty()) {
        *error = QStringLiteral("Minecraft services returned no access token");
        return false;
    }

    const JsonResult profileResult = getJson(QString::fromLatin1(kMinecraftProfileUrl),
                                             { { QStringLiteral("Authorization"), QStringLiteral("Bearer ") + account->accessToken } });
    const QJsonObject profile = parse(profileResult.body);
    if (!profileResult.ok) {
        *error = QStringLiteral("this account does not own Minecraft");
        return false;
    }

    account->name = profile.value(QStringLiteral("name")).toString();
    account->uuid = profile.value(QStringLiteral("id")).toString();
    if (account->name.isEmpty() || account->uuid.isEmpty()) {
        *error = QStringLiteral("the profile came back without a name");
        return false;
    }
    return true;
}

/// Xbox Live, then XSTS, then Minecraft. Common to signing in and to refreshing.
bool exchangeXbox(const QString& accessToken, MinecraftAccount* account, QString* error)
{
    const QJsonObject xboxUser{
        { QStringLiteral("Properties"),
          QJsonObject{ { QStringLiteral("AuthMethod"), QStringLiteral("RPS") },
                       { QStringLiteral("SiteName"), QStringLiteral("user.auth.xboxlive.com") },
                       { QStringLiteral("RpsTicket"), QStringLiteral("d=") + accessToken } } },
        { QStringLiteral("RelyingParty"), QStringLiteral("http://auth.xboxlive.com") },
        { QStringLiteral("TokenType"), QStringLiteral("JWT") },
    };
    const JsonResult xboxResult = postJson(QString::fromLatin1(kXboxUserUrl),
                                           QJsonDocument(xboxUser).toJson(QJsonDocument::Compact));
    const QJsonObject xboxBody = parse(xboxResult.body);
    if (!xboxResult.ok) {
        *error = QStringLiteral("Xbox Live refused the token: %1").arg(xboxBody.value(QStringLiteral("error")).toString(xboxResult.error));
        return false;
    }
    const QString xboxToken = xboxBody.value(QStringLiteral("Token")).toString();

    const QJsonObject xsts{
        { QStringLiteral("Properties"),
          QJsonObject{ { QStringLiteral("SandboxId"), QStringLiteral("RETAIL") },
                       { QStringLiteral("UserTokens"), QJsonArray{ xboxToken } } } },
        { QStringLiteral("RelyingParty"), QStringLiteral("rp://api.minecraftservices.com/") },
        { QStringLiteral("TokenType"), QStringLiteral("JWT") },
    };
    const JsonResult xstsResult = postJson(QString::fromLatin1(kXstsUrl),
                                           QJsonDocument(xsts).toJson(QJsonDocument::Compact));
    const QJsonObject xstsBody = parse(xstsResult.body);
    if (!xstsResult.ok) {
        const QString xerr = xstsBody.value(QStringLiteral("XErr")).toString();
        if (xerr == QLatin1String("2148916233")) {
            *error = QStringLiteral("this Microsoft account has no Xbox profile yet; sign in to Xbox once");
        } else if (xerr == QLatin1String("2148916238")) {
            *error = QStringLiteral("this account belongs to a child and needs a family group");
        } else {
            *error = QStringLiteral("XSTS refused the token: %1").arg(xstsBody.value(QStringLiteral("Message")).toString(xerr));
        }
        return false;
    }

    const QString xstsToken = xstsBody.value(QStringLiteral("Token")).toString();
    const QString userHash = xstsBody.value(QStringLiteral("DisplayClaims")).toObject()
                                 .value(QStringLiteral("xui")).toArray().first().toObject()
                                 .value(QStringLiteral("uhs")).toString();
    if (xstsToken.isEmpty() || userHash.isEmpty()) {
        *error = QStringLiteral("XSTS returned no token");
        return false;
    }
    account->xuid = xstsBody.value(QStringLiteral("DisplayClaims")).toObject()
                        .value(QStringLiteral("xui")).toArray().first().toObject()
                        .value(QStringLiteral("xid")).toString();
    return finishSignIn(xstsToken, userHash, account, error);
}

}  // namespace

QString MicrosoftAuth::clientId()
{
    // The environment variable exists for test runs that must not reach the real application, and
    // is otherwise unset. The id below is the launcher's own, so a player has nothing to fill in:
    // an application registered by hand almost always ends up refused by Microsoft (tenant type,
    // missing public client flows), which is a support ticket rather than a feature.
    const QString fromEnvironment = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KOMOREBI_MSA_CLIENT_ID"));
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    // The Komorebi application, the same one the full edition signs in with: a public client that
    // allows the device code flow this launcher uses.
    return QStringLiteral("bbee7876-69f0-40c7-980c-b717f438de9c");
}

DeviceCode MicrosoftAuth::startDeviceCode(QString* error)
{
    DeviceCode code;

    const QString client = clientId();

    const QByteArray body = form({
        { QStringLiteral("client_id"), client },
        { QStringLiteral("scope"), QString::fromLatin1(kScope) },
    });
    const JsonResult result = postJson(QString::fromLatin1(kDeviceCodeUrl), body,
                                       { { QStringLiteral("Content-Type"), QStringLiteral("application/x-www-form-urlencoded") } });
    const QJsonObject object = parse(result.body);
    if (!result.ok) {
        *error = object.value(QStringLiteral("error_description")).toString(result.error);
        return code;
    }

    code.deviceCode = object.value(QStringLiteral("device_code")).toString();
    code.userCode = object.value(QStringLiteral("user_code")).toString();
    code.verificationUri = object.value(QStringLiteral("verification_uri")).toString();
    code.interval = qMax(1, object.value(QStringLiteral("interval")).toInt(5));
    code.expiresIn = object.value(QStringLiteral("expires_in")).toInt(900);
    if (code.deviceCode.isEmpty() || code.userCode.isEmpty())
        *error = QStringLiteral("Microsoft returned no device code");
    return code;
}

bool MicrosoftAuth::signIn(const DeviceCode& code, MinecraftAccount* account, QString* error,
                           const std::function<bool()>& keepWaiting)
{
    const QString client = clientId();

    // Poll until the player has typed the code and approved it. Microsoft answers
    // "authorization_pending" while the browser is still open.
    QString accessToken;
    QString refreshToken;
    for (int elapsed = 0; elapsed < code.expiresIn; elapsed += code.interval) {
        QThread::msleep(static_cast<unsigned long>(code.interval) * 1000);
        if (keepWaiting && !keepWaiting()) {
            *error = QStringLiteral("cancelled");
            return false;
        }

        const QByteArray body = form({
            { QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") },
            { QStringLiteral("client_id"), client },
            { QStringLiteral("device_code"), code.deviceCode },
        });
        const JsonResult result = postJson(QString::fromLatin1(kTokenUrl), body,
                                           { { QStringLiteral("Content-Type"), QStringLiteral("application/x-www-form-urlencoded") } });
        const QJsonObject object = parse(result.body);
        if (result.ok) {
            accessToken = object.value(QStringLiteral("access_token")).toString();
            refreshToken = object.value(QStringLiteral("refresh_token")).toString();
            break;
        }

        const QString pending = object.value(QStringLiteral("error")).toString();
        if (pending != QLatin1String("authorization_pending") && pending != QLatin1String("slow_down")) {
            *error = object.value(QStringLiteral("error_description")).toString(pending);
            if (error->isEmpty())
                *error = QStringLiteral("sign-in failed");
            return false;
        }
    }

    if (accessToken.isEmpty()) {
        *error = QStringLiteral("the code expired before the sign-in finished");
        return false;
    }

    account->refreshToken = refreshToken;
    return exchangeXbox(accessToken, account, error);
}

PollResult MicrosoftAuth::poll(const DeviceCode& code)
{
    PollResult result;

    const QString client = clientId();

    const QByteArray body = form({
        { QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") },
        { QStringLiteral("client_id"), client },
        { QStringLiteral("device_code"), code.deviceCode },
    });
    const JsonResult answer = postJson(QString::fromLatin1(kTokenUrl), body,
                                       { { QStringLiteral("Content-Type"), QStringLiteral("application/x-www-form-urlencoded") } });
    const QJsonObject object = parse(answer.body);

    if (!answer.ok) {
        const QString pending = object.value(QStringLiteral("error")).toString();
        if (pending == QLatin1String("authorization_pending")) {
            result.state = PollResult::State::Pending;
            return result;
        }
        if (pending == QLatin1String("slow_down")) {
            result.state = PollResult::State::SlowDown;
            return result;
        }
        result.state = PollResult::State::Failed;
        result.error = object.value(QStringLiteral("error_description")).toString(pending);
        if (result.error.isEmpty())
            result.error = QStringLiteral("sign-in failed");
        return result;
    }

    const QString accessToken = object.value(QStringLiteral("access_token")).toString();
    if (accessToken.isEmpty()) {
        result.state = PollResult::State::Failed;
        result.error = QStringLiteral("no access token came back");
        return result;
    }

    result.account.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    QString error;
    if (!exchangeXbox(accessToken, &result.account, &error)) {
        result.state = PollResult::State::Failed;
        result.error = error;
        return result;
    }

    result.state = PollResult::State::Done;
    return result;
}
bool MicrosoftAuth::refresh(MinecraftAccount* account, QString* error)
{
    const QString client = clientId();
    if (account->refreshToken.isEmpty()) {
        *error = QStringLiteral("nothing to refresh with");
        return false;
    }

    const QByteArray body = form({
        { QStringLiteral("grant_type"), QStringLiteral("refresh_token") },
        { QStringLiteral("client_id"), client },
        { QStringLiteral("refresh_token"), account->refreshToken },
        { QStringLiteral("scope"), QString::fromLatin1(kScope) },
    });
    const JsonResult result = postJson(QString::fromLatin1(kTokenUrl), body,
                                       { { QStringLiteral("Content-Type"), QStringLiteral("application/x-www-form-urlencoded") } });
    const QJsonObject object = parse(result.body);
    if (!result.ok) {
        *error = object.value(QStringLiteral("error_description")).toString(result.error);
        return false;
    }

    const QString accessToken = object.value(QStringLiteral("access_token")).toString();
    const QString refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    if (accessToken.isEmpty()) {
        *error = QStringLiteral("no access token came back");
        return false;
    }
    if (!refreshToken.isEmpty())
        account->refreshToken = refreshToken;
    return exchangeXbox(accessToken, account, error);
}

}  // namespace Komorebi
