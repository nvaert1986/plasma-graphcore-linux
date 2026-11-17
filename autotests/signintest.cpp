// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signin.h"

#include <QTest>
#include <QUrlQuery>

using namespace GraphCore;

class SignInTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void isGuid()
    {
        QVERIFY(SignIn::isGuid(QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789")));
        QVERIFY(SignIn::isGuid(QStringLiteral("0A1B2C3D-4E5F-6789-ABCD-EF0123456789")));
        QVERIFY(!SignIn::isGuid(QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef012345678")));
        QVERIFY(!SignIn::isGuid(QStringLiteral(" 0a1b2c3d-4e5f-6789-abcd-ef0123456789")));
        QVERIFY(!SignIn::isGuid(QStringLiteral("contoso.onmicrosoft.com")));
    }

    void normalizedId()
    {
        QCOMPARE(SignIn::normalizedId(QStringLiteral(" 0A1B2C3D-4E5F-6789-ABCD-EF0123456789\n")), QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789"));
    }

    void parseSetupText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("clientId");
        QTest::addColumn<QString>("tenant");
        const QString client = QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789");
        const QString tenant = QStringLiteral("11111111-2222-3333-4444-555555555555");
        QTest::newRow("rc group") << QStringLiteral("[Auth]\nClientId=%1\nTenantId=%2\n").arg(client, tenant) << client << tenant;
        QTest::newRow("one line") << QStringLiteral("ClientId=%1 TenantId=%2").arg(client, tenant) << client << tenant;
        QTest::newRow("colons, domain") << QStringLiteral("clientid: %1 tenantid: contoso.com").arg(client) << client << QStringLiteral("contoso.com");
        QTest::newRow("client only") << QStringLiteral("ClientId = %1").arg(client) << client << QString();
        QTest::newRow("bare id is not setup text") << client << QString() << QString();
    }
    void parseSetupText()
    {
        QFETCH(QString, text);
        QFETCH(QString, clientId);
        QFETCH(QString, tenant);
        const SignIn::SetupText setup = SignIn::parseSetupText(text);
        QCOMPARE(setup.clientId, clientId);
        QCOMPARE(setup.tenant, tenant);
    }

    void resolveTenantWithoutNetwork()
    {
        bool offline = true;
        // A tenant ID needs no lookup; rubbish is refused before any request
        QCOMPARE(SignIn::resolveTenant(QStringLiteral(" 11111111-2222-3333-4444-55555555555A "), &offline),
                 QStringLiteral("11111111-2222-3333-4444-55555555555a"));
        QVERIFY(!offline);
        QCOMPARE(SignIn::resolveTenant(QStringLiteral("not a domain"), &offline), QString());
        QCOMPARE(SignIn::resolveTenant(QStringLiteral("someone@"), &offline), QString());
        QVERIFY(!offline);
    }

    void adminConsentUrl()
    {
        const QUrl url = SignIn::adminConsentUrl(QStringLiteral("tenant-1"), QStringLiteral("client-1"));
        QCOMPARE(url.host(), QStringLiteral("login.microsoftonline.com"));
        QCOMPARE(url.path(), QStringLiteral("/tenant-1/adminconsent"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("client_id")), QStringLiteral("client-1"));
        QCOMPARE(SignIn::adminConsentUrl({}, QStringLiteral("c")).path(), QStringLiteral("/organizations/adminconsent"));
    }

    void needsAdminConsent_data()
    {
        QTest::addColumn<QString>("error");
        QTest::addColumn<bool>("consent");
        QTest::newRow("not consented") << QStringLiteral("AADSTS65001: The user or administrator has not consented") << true;
        QTest::newRow("admin approval") << QStringLiteral("AADSTS90094: Admin approval required") << true;
        QTest::newRow("app not in directory") << QStringLiteral("AADSTS700016: Application not found") << true;
        QTest::newRow("browser answer") << QStringLiteral("consent_required: ...") << true;
        QTest::newRow("expired token") << QStringLiteral("AADSTS70008: The refresh token has expired") << false;
        QTest::newRow("longer code") << QStringLiteral("AADSTS650010: something else") << false;
    }
    void needsAdminConsent()
    {
        QFETCH(QString, error);
        QFETCH(bool, consent);
        QCOMPARE(SignIn::needsAdminConsent(error), consent);
    }
};

QTEST_GUILESS_MAIN(SignInTest)

#include "signintest.moc"
