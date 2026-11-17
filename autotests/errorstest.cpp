// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "errors.h"
#include "graphresponse.h"

#include <QTest>

using namespace GraphCore;

class ErrorsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // The English source texts, whatever the test machine's language
        qputenv("LANGUAGE", "en_US");
    }

    void okIsEmpty()
    {
        GraphResponse response;
        response.status = 200;
        QVERIFY(response.userMessage().isEmpty());
        QVERIFY(Errors::problemMessage({}).isEmpty());
    }

    void statusAndCode()
    {
        GraphResponse response;
        response.status = 403;
        response.body = R"({"error":{"code":"accessDenied","message":"Access denied"}})";
        QCOMPARE(response.userMessage(), QStringLiteral("You do not have permission for this. (HTTP 403 accessDenied)"));
        response.status = 507;
        response.body = R"({"error":{"code":"insufficientStorage","message":"x"}})";
        QVERIFY(response.userMessage().startsWith(QStringLiteral("There is not enough storage space online")));
        response.status = 429;
        response.body.clear();
        QCOMPARE(response.userMessage(), QStringLiteral("Microsoft 365 is limiting the number of requests for a while. It is tried again shortly. (HTTP 429)"));
    }

    void codeBeforeStatus()
    {
        // Graph's code says more than the status
        QVERIFY(Errors::message(403, QStringLiteral("notAllowed")).startsWith(QStringLiteral("Your organisation does not allow this.")));
        QVERIFY(Errors::message(400, QStringLiteral("ErrorQuotaExceeded")).startsWith(QStringLiteral("There is not enough storage space")));
        QVERIFY(Errors::message(409, QStringLiteral("nameAlreadyExists")).startsWith(QStringLiteral("An item with this name already exists.")));
    }

    void noAnswer()
    {
        QVERIFY(Errors::message(0, {}, QStringLiteral("Host graph.microsoft.com not found")).startsWith(QStringLiteral("No connection to Microsoft 365.")));
        QVERIFY(Errors::message(0, {}, QStringLiteral("Proxy connection refused")).startsWith(QStringLiteral("The proxy server")));
        QVERIFY(Errors::message(0, {}, QStringLiteral("SSL handshake failed")).startsWith(QStringLiteral("The secure connection")));
        QVERIFY(Errors::message(0, {}, QStringLiteral("Socket operation timed out")).startsWith(QStringLiteral("Microsoft 365 did not answer in time.")));
        QCOMPARE(Errors::message(0, {}, QStringLiteral("Operation canceled")), QStringLiteral("Cancelled."));
    }

    void problems()
    {
        QVERIFY(Errors::problemMessage(QStringLiteral("SignInExpired")).contains(QStringLiteral("Sign in again")));
        QVERIFY(Errors::problemMessage(QStringLiteral("KeychainLocked")).contains(QStringLiteral("locked")));
        QVERIFY(Errors::problemMessage(QStringLiteral("Whatever")).contains(QStringLiteral("(Whatever)")));
    }
};

QTEST_GUILESS_MAIN(ErrorsTest)
#include "errorstest.moc"
