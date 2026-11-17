// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The account service end to end: graphaccountsd_test on a private session bus (ctest runs this
// under dbus-run-session with test-session.conf), a fake login server in place of Microsoft.
// Nothing reaches the user's session bus or keychain.

#include "fakegraphserver.h"
#include "serviceclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrlQuery>
#include <QtConcurrentRun>

using namespace GraphCore;

namespace
{
const QString client = QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789");
const QString tenant = QStringLiteral("11111111-2222-3333-4444-555555555555");

QString userId(int n)
{
    return QStringLiteral("99999999-8888-7777-6666-55555555555%1").arg(n);
}

QString makeJwt(const QJsonObject &claims)
{
    const QByteArray header = QByteArray(R"({"alg":"none"})").toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray payload = QJsonDocument(claims).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(header + '.' + payload + ".sig");
}

FakeReply loginError(const char *code, const char *description)
{
    return jsonReply(400, QJsonDocument(QJsonObject{{QStringLiteral("error"), QLatin1String(code)}, {QStringLiteral("error_description"), QLatin1String(description)}})
                              .toJson(QJsonDocument::Compact));
}

// org.freedesktop.Notifications on the private bus: what the service shows
class FakeNotifications : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    struct Shown {
        QString summary;
        QString body;
        QStringList actions;
    };
    QList<Shown> shown;
    QList<uint> closed;

public Q_SLOTS:
    uint Notify(const QString &, uint, const QString &, const QString &summary, const QString &body, const QStringList &actions, const QVariantMap &, int)
    {
        shown.append({summary, body, actions});
        return uint(shown.size());
    }
    void CloseNotification(uint id)
    {
        closed.append(id);
    }
    QStringList GetCapabilities()
    {
        return {QStringLiteral("actions"), QStringLiteral("body"), QStringLiteral("persistence")};
    }
    QString GetServerInformation(QString &vendor, QString &version, QString &specVersion)
    {
        vendor = QStringLiteral("test");
        version = QStringLiteral("1");
        specVersion = QStringLiteral("1.2");
        return QStringLiteral("fake");
    }

Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action);
};

// The state the service gives an account
QString stateOf(const QString &id)
{
    const QList<ServiceAccount> all = AccountsClient().accounts();
    for (const ServiceAccount &a : all) {
        if (a.id == id) {
            return a.state;
        }
    }
    return QStringLiteral("(missing)");
}

// Runs a blocking call on another thread while this one keeps serving the fake login server
template<typename F>
auto meanwhile(F work)
{
    QFuture future = QtConcurrent::run(work);
    QElapsedTimer waited;
    waited.start();
    while (!future.isFinished() && waited.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return future.result();
}
}

class ServiceTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    std::unique_ptr<FakeGraphServer> login;
    QProcess daemon;
    FakeNotifications notifications;

private Q_SLOTS:
    void initTestCase()
    {
        if (qEnvironmentVariable("GRAPHACCOUNTS_PRIVATE_BUS") != QLatin1String("1")) {
            QSKIP("only on the private test bus (run through ctest)");
        }
        QVERIFY(dir.isValid());
        QDir().mkpath(dir.filePath(QStringLiteral("config")));
        QFile rc(dir.filePath(QStringLiteral("config/graphaccountsrc")));
        QVERIFY(rc.open(QIODevice::WriteOnly));
        // a1 signed in, a2 never here (its keychain cannot be read on this bus), a3 expired, a4 needs
        // an administrator's approval, a5 signed in without a stored organisation name
        QString text = QStringLiteral("[Registration]\nClientId=%1\n\n").arg(client);
        for (int n = 1; n <= 5; ++n) {
            text += QStringLiteral("[Account a%1]\nTenantId=%2\nObjectId=%3\nUserPrincipalName=user%1@example.com\nDisplayName=User %1\n%4\n")
                        .arg(n)
                        .arg(tenant, userId(n), n == 5 ? QString() : QStringLiteral("Organization=Example\n"));
        }
        text += QStringLiteral("[Development]\nAllowedExecutables=%1\n").arg(QCoreApplication::applicationFilePath());
        rc.write(text.toUtf8());
        rc.close();

        login = std::make_unique<FakeGraphServer>();
        login->handler = [](const FakeRequest &request) -> FakeReply {
            const QUrlQuery form(QString::fromUtf8(request.body));
            const QString path = QString::fromUtf8(request.target);
            if (path.endsWith(QLatin1String("/devicecode"))) {
                // (No raw string here: moc reads the "//" of an address in one as a comment)
                return jsonReply(200, QJsonDocument(QJsonObject{{QStringLiteral("device_code"), QStringLiteral("dc-1")},
                                                                {QStringLiteral("user_code"), QStringLiteral("ABCD-EFGH")},
                                                                {QStringLiteral("verification_uri"), QStringLiteral("https://example.com/devicelogin")},
                                                                {QStringLiteral("interval"), 1},
                                                                {QStringLiteral("expires_in"), 60}})
                                          .toJson(QJsonDocument::Compact));
            }
            if (form.queryItemValue(QStringLiteral("grant_type")).contains(QLatin1String("device_code"))) {
                // A complete sign-in; storing it fails here (no keychain on this bus)
                const QString id = makeJwt({{QStringLiteral("oid"), userId(4)}, {QStringLiteral("tid"), tenant}, {QStringLiteral("preferred_username"), QStringLiteral("user4@example.com")}});
                return jsonReply(200, QJsonDocument(QJsonObject{{QStringLiteral("access_token"), QStringLiteral("at-new")},
                                                                {QStringLiteral("refresh_token"), QStringLiteral("rt-new")},
                                                                {QStringLiteral("id_token"), id},
                                                                {QStringLiteral("expires_in"), 3600}})
                                          .toJson(QJsonDocument::Compact));
            }
            if (form.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded).contains(QLatin1String("User.Read.All"))) {
                return loginError("invalid_grant", "AADSTS65001: The user or administrator has not consented to use the application.");
            }
            const QString refresh = form.queryItemValue(QStringLiteral("refresh_token"));
            if (refresh == QLatin1String("rt-consent")) {
                return loginError("invalid_grant", "AADSTS65001: The user or administrator has not consented to use the application.");
            }
            if (request.target.endsWith("/v1.0/organization?$select=displayName")) {
                return jsonReply(200, R"({"value":[{"displayName":"Example Organisation"}]})");
            }
            if (refresh == QLatin1String("rt-expired")) {
                return loginError("invalid_grant", "AADSTS70008: The refresh token has expired");
            }
            return jsonReply(200, QJsonDocument(QJsonObject{{QStringLiteral("access_token"), QString(QStringLiteral("at-") + refresh)}, {QStringLiteral("expires_in"), 3600}})
                                      .toJson(QJsonDocument::Compact));
        };

        // The service's notifications go to the fake server; its event file from the source tree
        QVERIFY(QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/freedesktop/Notifications"), &notifications, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(QDBusConnection::sessionBus().registerService(QStringLiteral("org.freedesktop.Notifications")));
        QDir().mkpath(dir.filePath(QStringLiteral("data/knotifications6")));
        QVERIFY(QFile::copy(QStringLiteral(GRAPHCORE_NOTIFYRC), dir.filePath(QStringLiteral("data/knotifications6/plasma-graphcore-linux.notifyrc"))));

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), dir.filePath(QStringLiteral("config")));
        env.insert(QStringLiteral("XDG_DATA_HOME"), dir.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("XDG_STATE_HOME"), dir.filePath(QStringLiteral("state")));
        env.insert(QStringLiteral("GRAPHACCOUNTS_TEST_LOGIN_SERVER"), QStringLiteral("http://127.0.0.1:%1").arg(login->port()));
        env.insert(QStringLiteral("GRAPHACCOUNTS_TEST_REFRESH_TOKENS"), QStringLiteral("a1=rt-good,a3=rt-expired,a4=rt-consent,a5=rt-good"));
        env.insert(QStringLiteral("GRAPHACCOUNTS_TEST_GRAPH_SERVER"), QStringLiteral("http://127.0.0.1:%1/v1.0").arg(login->port()));
        // A "display" so the service is a GUI application (notifications); offscreen, nothing appears
        env.remove(QStringLiteral("DISPLAY"));
        env.insert(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("graphcore-test-no-display"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("en_US"));
        daemon.setProcessEnvironment(env);
        daemon.setProcessChannelMode(QProcess::ForwardedChannels);
        daemon.start(QStringLiteral(GRAPHACCOUNTSD_TEST), {});
        QVERIFY(daemon.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QDBusConnection::sessionBus().interface()->isServiceRegistered(AccountService::serviceName()), 10000);
    }

    void cleanupTestCase()
    {
        if (daemon.state() != QProcess::NotRunning) {
            daemon.terminate();
            daemon.waitForFinished(5000);
        }
    }

    void accountsListed()
    {
        AccountsClient accounts;
        QString error;
        const QList<ServiceAccount> all = accounts.accounts(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(all.size(), 5);
        QStringList names;
        for (const ServiceAccount &a : all) {
            names.append(a.userPrincipalName);
            QCOMPARE(a.state, QStringLiteral("unknown"));
        }
        names.sort();
        QCOMPARE(names,
                 QStringList({QStringLiteral("user1@example.com"), QStringLiteral("user2@example.com"), QStringLiteral("user3@example.com"), QStringLiteral("user4@example.com"),
                              QStringLiteral("user5@example.com")}));
    }

    void tokenForASignedInAccount()
    {
        AccountsClient accounts;
        QSignalSpy changed(&accounts, &AccountsClient::accountsChanged);
        ServiceTokenProvider tokens(QStringLiteral("a1"));
        QString error;
        QCOMPARE(meanwhile([&] {
                     return tokens.accessToken(&error);
                 }),
                 QStringLiteral("at-rt-good"));
        QVERIFY(tokens.lastProblem().isEmpty());
        const int asked = int(login->requests.size());
        // Kept by the client until shortly before it expires: no second request
        QCOMPARE(tokens.accessToken(&error), QStringLiteral("at-rt-good"));
        QCOMPARE(tokens.cachedAccessToken(), QStringLiteral("at-rt-good"));
        QCOMPARE(int(login->requests.size()), asked);
        // The account counts as signed in now, and the apps heard of it
        QTRY_VERIFY(!changed.isEmpty());
        QCOMPARE(stateOf(QStringLiteral("a1")), QStringLiteral("signedIn"));
        // forceRefresh (after a 401 from Graph) asks the service again
        QCOMPARE(meanwhile([&] {
                     return tokens.accessToken(&error, true);
                 }),
                 QStringLiteral("at-rt-good"));
        QCOMPARE(int(login->requests.size()), asked + 1);
    }

    void directoryWithoutConsentLeavesTheSignIn()
    {
        AccountsClient accounts;
        QSignalSpy needed(&accounts, &AccountsClient::signInNeeded);
        ServiceTokenProvider tokens(QStringLiteral("a1"), AccountService::directory());
        QString error;
        QVERIFY(meanwhile([&] {
                    return tokens.accessToken(&error);
                }).isEmpty());
        QCOMPARE(tokens.lastProblem(), QStringLiteral("ConsentRequired"));
        QVERIFY(error.contains(QLatin1String("AADSTS65001")));
        QTest::qWait(200);
        QVERIFY(needed.isEmpty());
        QCOMPARE(stateOf(QStringLiteral("a1")), QStringLiteral("signedIn"));
    }

    void expiredSignInIsAnnouncedOnce()
    {
        AccountsClient accounts;
        QSignalSpy needed(&accounts, &AccountsClient::signInNeeded);
        ServiceTokenProvider tokens(QStringLiteral("a3"));
        QString error;
        QVERIFY(meanwhile([&] {
                    return tokens.accessToken(&error);
                }).isEmpty());
        QCOMPARE(tokens.lastProblem(), QStringLiteral("SignInExpired"));
        QTRY_COMPARE(needed.size(), 1);
        QCOMPARE(needed.at(0).at(0).toString(), QStringLiteral("a3"));
        QCOMPARE(needed.at(0).at(1).toString(), QStringLiteral("SignInExpired"));
        QCOMPARE(stateOf(QStringLiteral("a3")), QStringLiteral("signInNeeded"));
        // One notification for the whole suite, with the account and a Sign In action
        QTRY_COMPARE(notifications.shown.size(), 1);
        QCOMPARE(notifications.shown.at(0).summary, QStringLiteral("Sign in again"));
        QVERIFY(notifications.shown.at(0).body.contains(QLatin1String("user3@example.com")));
        QVERIFY(notifications.shown.at(0).actions.contains(QStringLiteral("Sign In")));
        // Asked again: still refused, not announced again
        meanwhile([&] {
            return tokens.accessToken(&error);
        });
        QTest::qWait(200);
        QCOMPARE(needed.size(), 1);
        QCOMPARE(notifications.shown.size(), 1);
    }

    void approvalNeededIsAnnounced()
    {
        // Missing admin consent for the suite's own permissions: "Approval needed", not "Sign in again"
        const qsizetype before = notifications.shown.size();
        ServiceTokenProvider tokens(QStringLiteral("a4"));
        QString error;
        QVERIFY(meanwhile([&] {
                    return tokens.accessToken(&error);
                }).isEmpty());
        QCOMPARE(tokens.lastProblem(), QStringLiteral("ConsentRequired"));
        QTRY_COMPARE(notifications.shown.size(), before + 1);
        const FakeNotifications::Shown shown = notifications.shown.last();
        QCOMPARE(shown.summary, QStringLiteral("Approval needed"));
        QVERIFY(shown.body.contains(QLatin1String("user4@example.com")));
        QVERIFY(shown.actions.contains(QStringLiteral("Show Details")));
        QVERIFY(!shown.actions.contains(QStringLiteral("Sign In")));
    }

    void missingOrganizationIsFilledIn()
    {
        // A token for an account without an organisation name: the service fetches and stores it once
        AccountsClient accounts;
        QSignalSpy changed(&accounts, &AccountsClient::accountsChanged);
        ServiceTokenProvider tokens(QStringLiteral("a5"));
        QString error;
        QVERIFY(!meanwhile([&] {
                     return tokens.accessToken(&error);
                 }).isEmpty());
        const auto organizationOf = [&accounts] {
            const QList<ServiceAccount> all = accounts.accounts();
            for (const ServiceAccount &a : all) {
                if (a.id == QLatin1String("a5")) {
                    return a.organization;
                }
            }
            return QString();
        };
        QTRY_COMPARE_WITH_TIMEOUT(organizationOf(), QStringLiteral("Example Organisation"), 10000);
        QVERIFY(!changed.isEmpty());
        // Kept in graphaccountsrc for the apps
        QFile rc(dir.filePath(QStringLiteral("config/graphaccountsrc")));
        QVERIFY(rc.open(QIODevice::ReadOnly));
        QVERIFY(rc.readAll().contains("Organization=Example Organisation"));
    }

    void unreadableKeychainIsNotASignOut()
    {
        // a2 has no sign-in in memory and the keychain cannot be read on this bus: that must say
        // "locked", never "not signed in" (the wallet at login is the real case)
        ServiceTokenProvider tokens(QStringLiteral("a2"));
        QString error;
        QVERIFY(meanwhile([&] {
                    return tokens.accessToken(&error);
                }).isEmpty());
        QCOMPARE(tokens.lastProblem(), QStringLiteral("KeychainLocked"));
        AccountsClient accounts;
        QCOMPARE(stateOf(QStringLiteral("a2")), QStringLiteral("keychainLocked"));
    }

    void badRequests()
    {
        ServiceTokenProvider unknown(QStringLiteral("no-such-account"));
        QString error;
        QVERIFY(unknown.accessToken(&error).isEmpty());
        QCOMPARE(unknown.lastProblem(), QStringLiteral("UnknownAccount"));

        ServiceTokenProvider odd(QStringLiteral("a1"), QStringLiteral("Mail.Read Files.ReadWrite.All"));
        QVERIFY(odd.accessToken(&error).isEmpty());
        QCOMPARE(odd.lastProblem(), QStringLiteral("InvalidArgs"));

        AccountsClient accounts;
        QVERIFY(accounts.signIn(QStringLiteral("not an id"), {}, {}, true, &error).isEmpty());
        QVERIFY(error.contains(QLatin1String("client ID")));
    }

    void otherProgramsAreRefused()
    {
        // dbus-send is not one of the suite's programs
        QProcess send;
        send.start(QStringLiteral("dbus-send"),
                   {QStringLiteral("--session"),
                    QStringLiteral("--print-reply"),
                    QStringLiteral("--dest=") + AccountService::serviceName(),
                    AccountService::objectPath(),
                    AccountService::interfaceName() + QStringLiteral(".Accounts")});
        QVERIFY(send.waitForFinished(10000));
        QVERIFY(send.exitCode() != 0);
        QVERIFY2(send.readAllStandardError().contains("NotAllowed"), "the refusal names NotAllowed");
    }

    void deviceCodeSignIn()
    {
        AccountsClient accounts;
        QSignalSpy progress(&accounts, &AccountsClient::signInProgress);
        QSignalSpy finished(&accounts, &AccountsClient::signInFinished);
        QString error;
        const QString request = accounts.signIn(client, tenant, {}, true, &error);
        QVERIFY2(!request.isEmpty(), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(progress.size(), 1, 10000);
        QCOMPARE(progress.at(0).at(0).toString(), request);
        QCOMPARE(progress.at(0).at(1).toString(), QStringLiteral("ABCD-EFGH"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QCOMPARE(finished.at(0).at(0).toString(), request);
        // Microsoft said yes, but the sign-in cannot be stored on this bus: it must not count
        QVERIFY(finished.at(0).at(1).toString().isEmpty());
        QVERIFY(!finished.at(0).at(2).toString().isEmpty());
        QCOMPARE(accounts.accounts().size(), 5);
    }

    void signOut()
    {
        AccountsClient accounts;
        QString error;
        QVERIFY2(meanwhile([&] {
                     return accounts.signOut(QStringLiteral("a1"), &error);
                 }),
                 qPrintable(error));
        QCOMPARE(stateOf(QStringLiteral("a1")), QStringLiteral("signedOut"));
        QVERIFY(!accounts.signOut(QStringLiteral("no-such-account"), &error));
    }
};

QTEST_GUILESS_MAIN(ServiceTest)

#include "servicetest.moc"
