// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "callercheck.h"

#include <QCoreApplication>
#include <QTest>

using namespace GraphCore;

class CallerCheckTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void suitePrograms()
    {
        const CallerCheck::Allowed allowed = CallerCheck::allowedFor({QStringLiteral("/usr/bin"), QStringLiteral("/home/someone/.local/bin/")}, {});
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/usr/bin/graphmaild"), allowed));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/usr/bin/graphsyncd"), allowed));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/usr/bin/graphchatd"), allowed));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/usr/bin/graphchat-call"), allowed));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/home/someone/.local/bin/plasma-graphmail-linux"), allowed));
        // Other programs, also in the same directories
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/usr/bin/dbus-send"), allowed));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/usr/bin/python3"), allowed));
        // A suite name elsewhere
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/tmp/graphmaild"), allowed));
        // Tricks with the path
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/usr/bin/../../tmp/graphmaild"), allowed));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/usr/bin/./graphmaild"), allowed));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("graphmaild"), allowed));
        QVERIFY(!CallerCheck::isAllowed(QString(), allowed));
    }

    void managers()
    {
        // System Settings manages accounts; it is not one of the suite's programs
        const CallerCheck::Allowed apps = CallerCheck::allowedFor({QStringLiteral("/usr/bin")}, {});
        const CallerCheck::Allowed managers = CallerCheck::allowedFor({QStringLiteral("/usr/bin")}, {}, CallerCheck::managerNames());
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/usr/bin/systemsettings"), apps));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/usr/bin/systemsettings"), managers));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/usr/bin/kcmshell6"), managers));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/usr/bin/graphmaild"), managers));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/home/someone/.local/bin/systemsettings"), managers));
    }

    void developmentEntries()
    {
        const CallerCheck::Allowed allowed = CallerCheck::allowedFor({}, {QStringLiteral("/src/mail/build/bin/"), QStringLiteral("/opt/tool"), QStringLiteral("relative/bin/")});
        // A build tree: every file directly in it
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/src/mail/build/bin/graphmaild"), allowed));
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/src/mail/build/bin/anything"), allowed));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/src/mail/build/bin/sub/graphmaild"), allowed));
        // One file
        QVERIFY(CallerCheck::isAllowed(QStringLiteral("/opt/tool"), allowed));
        QVERIFY(!CallerCheck::isAllowed(QStringLiteral("/opt/tool2"), allowed));
        // Relative entries are ignored
        QCOMPARE(allowed.directories, QStringList{QStringLiteral("/src/mail/build/bin")});
    }

    void ownExecutable()
    {
        QCOMPARE(CallerCheck::executableOf(QCoreApplication::applicationPid()), QCoreApplication::applicationFilePath());
        QVERIFY(CallerCheck::executableOf(0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(CallerCheckTest)

#include "callerchecktest.moc"
