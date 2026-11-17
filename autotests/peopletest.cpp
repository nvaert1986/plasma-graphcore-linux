// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "people.h"

#include <QJsonDocument>
#include <QTest>
#include <QUrl>

using namespace GraphCore;

class PeopleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void requests()
    {
        const People::Request all = People::relevant({}, 500);
        QVERIFY(all.path.startsWith(QStringLiteral("/me/people?$top=500&$select=")));
        QVERIFY(!all.path.contains(QStringLiteral("$search")));
        // Quotes cannot end the search term early
        const People::Request typed = People::relevant(QStringLiteral("ad\"ele  va"));
        QVERIFY(QUrl::fromPercentEncoding(typed.path.toLatin1()).endsWith(QStringLiteral("$search=\"adele va\"")));
        const People::Request dir = People::directory(QStringLiteral("megan"), 40);
        QVERIFY(dir.path.startsWith(QStringLiteral("/users?$top=40&$count=true&$select=id,displayName,mail")));
        QVERIFY(QUrl::fromPercentEncoding(dir.path.toLatin1()).endsWith(QStringLiteral("\"displayName:megan\" OR \"mail:megan\"")));
        QCOMPARE(dir.headers.size(), 1);
        QCOMPARE(dir.headers.first().first, QByteArrayLiteral("ConsistencyLevel"));
        QVERIFY(People::directory(QStringLiteral("x"), 8, {QStringLiteral("id"), QStringLiteral("mail")}).path.contains(QStringLiteral("$select=id,mail&")));
        QCOMPARE(People::photoPath(QStringLiteral("adele.vance@contoso.com")), QStringLiteral("/users/adele.vance@contoso.com/photos/96x96/$value"));
        QCOMPARE(People::photoPath(QStringLiteral("0a1b"), 48), QStringLiteral("/users/0a1b/photos/48x48/$value"));
    }

    void parseAndMerge()
    {
        const QJsonObject relevantAnswer = QJsonDocument::fromJson(R"({"value":[
            {"id":"p1","displayName":"Adele Vance","jobTitle":"Retail Manager","scoredEmailAddresses":[{"address":"adele.vance@contoso.com"},{"address":"adele@contoso.com"}]},
            {"id":"p2","displayName":"No Address","scoredEmailAddresses":[]}]})").object();
        const QJsonObject directoryAnswer = QJsonDocument::fromJson(R"({"value":[
            {"id":"u1","displayName":"Adele Vance","mail":"ADELE.VANCE@contoso.com"},
            {"id":"u2","displayName":"Room Account","mail":null,"userPrincipalName":"room@contoso.com"},
            {"id":"u3","displayName":"Megan Bowen","mail":"megan@contoso.com","department":"Marketing"}]})").object();
        const QList<People::Person> relevant = People::parseRelevant(relevantAnswer);
        QCOMPARE(relevant.size(), 2);
        QCOMPARE(relevant.at(0).email, QStringLiteral("adele.vance@contoso.com"));
        QCOMPARE(relevant.at(0).jobTitle, QStringLiteral("Retail Manager"));
        QCOMPARE(relevant.at(0).emails, QStringList({QStringLiteral("adele.vance@contoso.com"), QStringLiteral("adele@contoso.com")}));
        QVERIFY(relevant.at(1).emails.isEmpty());
        const QList<People::Person> directory = People::parseDirectory(directoryAnswer);
        QCOMPARE(directory.at(1).email, QStringLiteral("room@contoso.com"));
        // Relevant first, Adele once, the one without an address left out
        const QList<People::Person> merged = People::merge(relevant, directory, 8);
        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged.at(0).id, QStringLiteral("p1"));
        QCOMPARE(merged.at(1).email, QStringLiteral("room@contoso.com"));
        QCOMPARE(merged.at(2).department, QStringLiteral("Marketing"));
        QCOMPARE(People::merge(relevant, directory, 2).size(), 2);
    }
};

QTEST_GUILESS_MAIN(PeopleTest)
#include "peopletest.moc"
