// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "auth.h"
#include "fakegraphserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTest>
#include <QUrlQuery>

using namespace GraphCore;

// Nothing here reads or writes the real keychain: tokens come from setRefreshTokenForTesting and
// the fake login server never hands out a new refresh token

namespace
{
const QString client = QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789");
const QString tenant = QStringLiteral("11111111-2222-3333-4444-555555555555");
const QString user = QStringLiteral("99999999-8888-7777-6666-555555555555");

QString makeJwt(const QJsonObject &claims)
{
    const QByteArray header = QByteArray(R"({"alg":"none"})").toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray payload = QJsonDocument(claims).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(header + '.' + payload + ".sig");
}

AuthSettings signedIn()
{
    return {QStringLiteral("graphcore-test"), client, tenant, user, {QStringLiteral("User.Read")}};
}

FakeReply loginError(const char *code, const char *description)
{
    return jsonReply(400, QJsonDocument(QJsonObject{{QStringLiteral("error"), QLatin1String(code)}, {QStringLiteral("error_description"), QLatin1String(description)}})
                              .toJson(QJsonDocument::Compact));
}
}

class AuthTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeGraphServer> server;

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        server = std::make_unique<FakeGraphServer>();
        Auth::setLoginServerForTesting(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server->port())));
    }

    void cleanup()
    {
        Auth::setLoginServerForTesting({});
        server.reset();
    }

    void parseIdToken()
    {
        const QString token = makeJwt({{QStringLiteral("oid"), user.toUpper()},
                                       {QStringLiteral("tid"), tenant},
                                       {QStringLiteral("preferred_username"), QStringLiteral("someone@example.com")},
                                       {QStringLiteral("name"), QStringLiteral("Some One")}});
        const SignedInUser parsed = Auth::parseIdToken(token);
        // IDs as stored: lower case
        QCOMPARE(parsed.objectId, user);
        QCOMPARE(parsed.tenantId, tenant);
        QCOMPARE(parsed.userPrincipalName, QStringLiteral("someone@example.com"));
        QCOMPARE(parsed.displayName, QStringLiteral("Some One"));
        QVERIFY(Auth::parseIdToken(QStringLiteral("garbage")).objectId.isEmpty());
        // A token that names no real user is no sign-in at all
        QVERIFY(Auth::parseIdToken(makeJwt({{QStringLiteral("oid"), QStringLiteral("user-1")}, {QStringLiteral("tid"), tenant}})).objectId.isEmpty());
    }

    void settingsForAccount()
    {
        Accounts::setDefaultClientId(QStringLiteral("default-client"));
        Account account;
        account.tenantId = QStringLiteral("t");
        account.objectId = QStringLiteral("o");
        AuthSettings settings = Auth::settingsFor(account, QStringLiteral("svc"), {QStringLiteral("Mail.Read")});
        QCOMPARE(settings.clientId, QStringLiteral("default-client"));
        QCOMPARE(settings.tenant, QStringLiteral("t"));
        QCOMPARE(settings.objectId, QStringLiteral("o"));
        QCOMPARE(settings.keychainService, QStringLiteral("svc"));

        account.clientId = QStringLiteral("own-client");
        QCOMPARE(Auth::settingsFor(account, QStringLiteral("svc"), {}).clientId, QStringLiteral("own-client"));

        // Only a registration other than the default is remembered on the account
        const SignedInUser who{QStringLiteral("o"), QStringLiteral("u@example.com"), QStringLiteral("U"), QStringLiteral("t")};
        QVERIFY(Auth::accountFor(who, QStringLiteral("default-client")).clientId.isEmpty());
        QCOMPARE(Auth::accountFor(who, QStringLiteral("other")).clientId, QStringLiteral("other"));
    }

    void keyParts()
    {
        // A first sign-in: any work account of any organisation
        QVERIFY(Auth({QStringLiteral("svc"), client, QStringLiteral("organizations"), {}, {}}).isConfigured());
        QVERIFY(Auth(signedIn()).isConfigured());
        // Nothing that could break the key "tenant:client:user" or is not an ID
        QVERIFY(!Auth({QStringLiteral("svc"), QStringLiteral("c"), tenant, {}, {}}).isConfigured());
        QVERIFY(!Auth({QStringLiteral("svc"), client, QStringLiteral("a:b"), {}, {}}).isConfigured());
        QVERIFY(!Auth({QStringLiteral("svc"), client, tenant, QStringLiteral("someone"), {}}).isConfigured());
        QVERIFY(!Auth({QStringLiteral("svc"), client, QString(), {}, {}}).isConfigured());
        QCOMPARE(Auth::keychainKeyFor(tenant, client, user), tenant + QLatin1Char(':') + client + QLatin1Char(':') + user);
    }

    void notSignedInWithoutObjectId()
    {
        // Before a sign-in there is no user, so nothing is read from the keychain
        Auth auth({QStringLiteral("graphcore-test"), client, QStringLiteral("organizations"), {}, {}});
        QVERIFY(auth.cachedAccessToken().isEmpty());
        QString error;
        Auth::Problem problem = Auth::Problem::None;
        QVERIFY(auth.accessToken(&error, false, &problem).isEmpty());
        QCOMPARE(error, QStringLiteral("not signed in"));
        QCOMPARE(problem, Auth::Problem::NotSignedIn);
        QCOMPARE(auth.storedRefreshToken(), Auth::StoredToken::Missing);
        QVERIFY(server->requests.isEmpty());
    }

    void refreshGivesAToken()
    {
        server->handler = [](const FakeRequest &) {
            return jsonReply(200, R"({"access_token":"at-1","expires_in":3600})");
        };
        Auth auth(signedIn());
        auth.setRefreshTokenForTesting(QStringLiteral("rt-1"));
        QString error;
        Auth::Problem problem = Auth::Problem::Other;
        QCOMPARE(auth.accessToken(&error, false, &problem), QStringLiteral("at-1"));
        QCOMPARE(problem, Auth::Problem::None);
        QCOMPARE(server->requests.size(), 1);
        const FakeRequest &request = server->requests.first();
        QCOMPARE(request.target, QByteArray("/") + tenant.toLatin1() + "/oauth2/v2.0/token");
        const QUrlQuery form(QString::fromUtf8(request.body));
        QCOMPARE(form.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
        QCOMPARE(form.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("rt-1"));
        QVERIFY(form.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded).split(QLatin1Char(' ')).contains(QStringLiteral("offline_access")));
        // Cached: no second request
        QCOMPARE(auth.accessToken(&error), QStringLiteral("at-1"));
        QCOMPARE(auth.cachedAccessToken(), QStringLiteral("at-1"));
        QCOMPARE(server->requests.size(), 1);
    }

    void missingConsentKeepsTheSignIn()
    {
        // AADSTS65001 comes as invalid_grant, but the refresh token is fine: only these permissions
        // are not approved. The sign-in must stay (Graph Mail lost whole tokens to this once)
        server->handler = [](const FakeRequest &) {
            return loginError("invalid_grant", "AADSTS65001: The user or administrator has not consented to use the application.\nTrace ID: x");
        };
        Auth auth(signedIn());
        auth.setRefreshTokenForTesting(QStringLiteral("rt-1"));
        QString error;
        Auth::Problem problem = Auth::Problem::None;
        QVERIFY(auth.accessToken(&error, false, &problem).isEmpty());
        QCOMPARE(problem, Auth::Problem::ConsentRequired);
        QVERIFY(error.contains(QLatin1String("AADSTS65001")));
        QVERIFY(!error.contains(QLatin1String("Trace ID")));
        // Asked again: the same refresh token goes out (not read from the keychain, not gone)
        QVERIFY(auth.accessToken(&error, false, &problem).isEmpty());
        QCOMPARE(server->requests.size(), 2);
        QCOMPARE(QUrlQuery(QString::fromUtf8(server->requests.at(1).body)).queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("rt-1"));
    }

    void expiredSignIn()
    {
        server->handler = [](const FakeRequest &) {
            return loginError("invalid_grant", "AADSTS70008: The provided authorization code or refresh token has expired");
        };
        Auth auth(signedIn());
        auth.setRefreshTokenForTesting(QStringLiteral("rt-1"));
        QString error;
        Auth::Problem problem = Auth::Problem::None;
        QVERIFY(auth.accessToken(&error, false, &problem).isEmpty());
        QCOMPARE(problem, Auth::Problem::SignInExpired);
        QVERIFY(error.startsWith(QLatin1String("sign-in expired")));
        // (Not asked again: the dead token is dropped, a second call would read the keychain)
    }

    void otherRefusal()
    {
        server->handler = [](const FakeRequest &) {
            return loginError("invalid_request", "AADSTS900144: The request body must contain the following parameter");
        };
        Auth auth(signedIn());
        auth.setRefreshTokenForTesting(QStringLiteral("rt-1"));
        QString error;
        Auth::Problem problem = Auth::Problem::None;
        QVERIFY(auth.accessToken(&error, false, &problem).isEmpty());
        QCOMPARE(problem, Auth::Problem::Other);
    }

    void offline()
    {
        // A port nobody listens on: no answer at all
        quint16 closed = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            closed = probe.serverPort();
        }
        Auth::setLoginServerForTesting(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closed)));
        Auth auth(signedIn());
        auth.setRefreshTokenForTesting(QStringLiteral("rt-1"));
        QString error;
        Auth::Problem problem = Auth::Problem::None;
        QVERIFY(auth.accessToken(&error, false, &problem).isEmpty());
        QCOMPARE(problem, Auth::Problem::Offline);
        QVERIFY(error.startsWith(QLatin1String("no connection")));
    }
};

QTEST_GUILESS_MAIN(AuthTest)

#include "authtest.moc"
