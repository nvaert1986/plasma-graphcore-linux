// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fakegraphserver.h"
#include "updatecheck.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace GraphCore;

namespace
{
const QString repository = QStringLiteral("example/some-app");

// GitHub's answer to releases/latest, with the fields that matter
QByteArray answer(const QJsonObject &release)
{
    return QJsonDocument(release).toJson();
}
}

class UpdateCheckTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;

    QString stateFile() const
    {
        return dir.filePath(QStringLiteral("state/updatecheck"));
    }
    UpdateCheckSettings settings(const QUrl &latestUrl = {}) const
    {
        return {repository, QStringLiteral("0.12.0"), stateFile(), QStringLiteral("some-app/0.12.0"), latestUrl};
    }
    void setState(const char *key, const QString &value) const
    {
        KConfig config(stateFile(), KConfig::SimpleConfig);
        config.group(QStringLiteral("Update")).writeEntry(key, value);
        config.sync();
    }

private Q_SLOTS:
    void init()
    {
        QFile::remove(stateFile());
    }

    void parseLatest()
    {
        const auto release = UpdateCheck::parseLatest(answer({{QStringLiteral("tag_name"), QStringLiteral("v0.9.5")},
                                                              {QStringLiteral("draft"), false},
                                                              {QStringLiteral("prerelease"), false},
                                                              {QStringLiteral("html_url"), QStringLiteral("https://example.com/elsewhere")}}),
                                                      repository);
        QVERIFY(release);
        QCOMPARE(release->version, QStringLiteral("0.9.5"));
        // The page is made here, never taken from the answer
        QCOMPARE(release->page, QUrl(QStringLiteral("https://github.com/example/some-app/releases/tag/v0.9.5")));
        QCOMPARE(UpdateCheck::parseLatest(answer({{QStringLiteral("tag_name"), QStringLiteral("1.0")}}), repository)->version, QStringLiteral("1.0"));

        QVERIFY(!UpdateCheck::parseLatest(answer({{QStringLiteral("tag_name"), QStringLiteral("v1.0.0")}, {QStringLiteral("prerelease"), true}}), repository));
        QVERIFY(!UpdateCheck::parseLatest(answer({{QStringLiteral("tag_name"), QStringLiteral("v1.0.0")}, {QStringLiteral("draft"), true}}), repository));
        QVERIFY(!UpdateCheck::parseLatest(answer({{QStringLiteral("tag_name"), QStringLiteral("v1.0.0-beta1")}}), repository));
        QVERIFY(!UpdateCheck::parseLatest(answer({{QStringLiteral("tag_name"), QStringLiteral("v1.0/../../x")}}), repository));
        QVERIFY(!UpdateCheck::parseLatest(answer({{QStringLiteral("message"), QStringLiteral("API rate limit exceeded")}}), repository));
        QVERIFY(!UpdateCheck::parseLatest("not json", repository));
    }

    void isNewer()
    {
        QVERIFY(UpdateCheck::isNewer(QStringLiteral("0.9.10"), QStringLiteral("0.9.4")));
        QVERIFY(UpdateCheck::isNewer(QStringLiteral("1.0"), QStringLiteral("0.9.4")));
        QVERIFY(UpdateCheck::isNewer(QStringLiteral("v0.9.4.1"), QStringLiteral("0.9.4")));
        QVERIFY(!UpdateCheck::isNewer(QStringLiteral("0.9.4"), QStringLiteral("0.9.4")));
        QVERIFY(!UpdateCheck::isNewer(QStringLiteral("0.9.3"), QStringLiteral("0.9.4")));
        QVERIFY(!UpdateCheck::isNewer(QString(), QStringLiteral("0.9.4")));
        QVERIFY(!UpdateCheck::isNewer(QStringLiteral("garbage"), QStringLiteral("0.9.4")));
    }

    void announceUntilSkipped()
    {
        UpdateChecker checker(settings());
        QVERIFY(checker.available().isEmpty());
        QVERIFY(!checker.announce(true));

        // Older than or the same as this build: nothing to tell
        setState("Latest", QStringLiteral("0.12.0"));
        QVERIFY(checker.available().isEmpty());
        setState("Latest", QStringLiteral("0.1"));
        QVERIFY(checker.available().isEmpty());

        setState("Latest", QStringLiteral("99.0"));
        QCOMPARE(checker.available(), QStringLiteral("99.0"));
        QCOMPARE(checker.page(), QUrl(QStringLiteral("https://github.com/example/some-app/releases/tag/v99.0")));
        QVERIFY(checker.announce(false));
        checker.markAnnounced();
        // Not again the same day, but again at the next login and the next day
        QVERIFY(!checker.announce(false));
        QVERIFY(checker.announce(true));
        setState("AnnouncedAt", QDateTime::currentDateTimeUtc().addSecs(-24 * 3600).toString(Qt::ISODate));
        QVERIFY(checker.announce(false));
        QCOMPARE(checker.available(), QStringLiteral("99.0"));

        checker.skip(QStringLiteral("99.0"));
        QVERIFY(checker.available().isEmpty());
        QVERIFY(!checker.announce(false));
        QVERIFY(!checker.announce(true));

        // A later version after a skipped one is told about again
        setState("Latest", QStringLiteral("99.1"));
        QCOMPARE(checker.available(), QStringLiteral("99.1"));
        QVERIFY(checker.announce(false));
    }

    void asksOnceADay()
    {
        FakeGraphServer github;
        github.handler = [](const FakeRequest &) {
            return jsonReply(200, QJsonDocument(QJsonObject{{QStringLiteral("tag_name"), QStringLiteral("v0.13.0")}}).toJson(QJsonDocument::Compact));
        };
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/repos/example/some-app/releases/latest").arg(github.port()));
        UpdateChecker checker(settings(url));
        QSignalSpy checked(&checker, &UpdateChecker::checked);
        checker.check();
        QVERIFY(checked.wait(10000));
        QVERIFY(checked.at(0).at(0).toBool());
        QCOMPARE(github.requests.size(), 1);
        // GitHub wants a User-Agent
        QCOMPARE(github.requests.at(0).headers.value("user-agent"), QByteArray("some-app/0.12.0"));
        QCOMPARE(checker.available(), QStringLiteral("0.13.0"));
        QCOMPARE(checker.latest(), QStringLiteral("0.13.0"));
        QVERIFY(checker.lastCheck().isValid());

        // The same day: not asked again (the last answer counts), unless forced
        checker.check();
        QCOMPARE(checked.size(), 2);
        QCOMPARE(github.requests.size(), 1);
        checker.check(true);
        QTRY_COMPARE(checked.size(), 3);
        QCOMPARE(github.requests.size(), 2);
    }

    void refusedAnswer()
    {
        FakeGraphServer github;
        github.handler = [](const FakeRequest &) {
            return jsonReply(403, QJsonDocument(QJsonObject{{QStringLiteral("message"), QStringLiteral("API rate limit exceeded")}}).toJson(QJsonDocument::Compact));
        };
        UpdateChecker checker(settings(QUrl(QStringLiteral("http://127.0.0.1:%1/latest").arg(github.port()))));
        QSignalSpy checked(&checker, &UpdateChecker::checked);
        checker.check();
        QVERIFY(checked.wait(10000));
        QVERIFY(!checked.at(0).at(0).toBool());
        QVERIFY(checker.available().isEmpty());
        // Nothing stored: the next check asks again
        QVERIFY(!checker.lastCheck().isValid());
    }
};

QTEST_GUILESS_MAIN(UpdateCheckTest)

#include "updatechecktest.moc"
