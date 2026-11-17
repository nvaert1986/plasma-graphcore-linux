// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "proxy.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>

using namespace GraphCore;

class ProxyTest : public QObject
{
    Q_OBJECT

private:
    int m_writes = 0;

    void writeSettings(const QByteArray &body)
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QDir().mkpath(dir);
        QFile file(dir + QStringLiteral("/kioslaverc"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[Proxy Settings]\n" + body);
        file.close();
        // A new time for every write: the settings are read again when the file's time changes
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(++m_writes), QFileDevice::FileModificationTime));
    }

    static QNetworkProxy proxyFor(const QString &url)
    {
        const QList<QNetworkProxy> proxies = Proxy::proxiesFor(QNetworkProxyQuery(QUrl(url)));
        return proxies.isEmpty() ? QNetworkProxy() : proxies.constFirst();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kioslaverc"));
    }

    void noSettings()
    {
        cleanupTestCase();
        QCOMPARE(proxyFor(QStringLiteral("https://graph.microsoft.com/v1.0/me")).type(), QNetworkProxy::NoProxy);
    }

    void none()
    {
        writeSettings("ProxyType=0\nhttpsProxy=http://proxy.example.com 3128\n");
        QCOMPARE(proxyFor(QStringLiteral("https://graph.microsoft.com/v1.0/me")).type(), QNetworkProxy::NoProxy);
    }

    void manualKdeForm()
    {
        // KDE writes "scheme://host port"
        writeSettings("ProxyType=1\nhttpsProxy=http://proxy.example.com 3128\nhttpProxy=http://web.example.com 8080\n");
        const QNetworkProxy https = proxyFor(QStringLiteral("https://graph.microsoft.com/v1.0/me"));
        QCOMPARE(https.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(https.hostName(), QStringLiteral("proxy.example.com"));
        QCOMPARE(https.port(), quint16(3128));
        const QNetworkProxy http = proxyFor(QStringLiteral("http://example.org/"));
        QCOMPARE(http.hostName(), QStringLiteral("web.example.com"));
        QCOMPARE(http.port(), quint16(8080));
    }

    void manualUrlForm()
    {
        writeSettings("ProxyType=1\nhttpsProxy=proxy.example.com:3129\n");
        const QNetworkProxy proxy = proxyFor(QStringLiteral("https://login.microsoftonline.com/common"));
        QCOMPARE(proxy.hostName(), QStringLiteral("proxy.example.com"));
        QCOMPARE(proxy.port(), quint16(3129));
    }

    void httpsFallsBackToHttpThenSocks()
    {
        writeSettings("ProxyType=1\nhttpProxy=http://web.example.com 8080\n");
        QCOMPARE(proxyFor(QStringLiteral("https://graph.microsoft.com/")).hostName(), QStringLiteral("web.example.com"));
        writeSettings("ProxyType=1\nsocksProxy=socks://socks.example.com 1081\n");
        const QNetworkProxy socks = proxyFor(QStringLiteral("https://graph.microsoft.com/"));
        QCOMPARE(socks.type(), QNetworkProxy::Socks5Proxy);
        QCOMPARE(socks.hostName(), QStringLiteral("socks.example.com"));
        QCOMPARE(socks.port(), quint16(1081));
    }

    void exceptions()
    {
        writeSettings("ProxyType=1\nhttpsProxy=http://proxy.example.com 3128\nNoProxyFor=.internal.example,microsoft.com,10.0.0.0/8,<local>\n");
        QCOMPARE(proxyFor(QStringLiteral("https://graph.microsoft.com/")).type(), QNetworkProxy::NoProxy);
        QCOMPARE(proxyFor(QStringLiteral("https://files.internal.example/")).type(), QNetworkProxy::NoProxy);
        QCOMPARE(proxyFor(QStringLiteral("https://10.1.2.3/")).type(), QNetworkProxy::NoProxy);
        QCOMPARE(proxyFor(QStringLiteral("https://intranet/")).type(), QNetworkProxy::NoProxy);
        QCOMPARE(proxyFor(QStringLiteral("https://login.microsoftonline.com/")).type(), QNetworkProxy::HttpProxy);
        QCOMPARE(proxyFor(QStringLiteral("https://notmicrosoft.com/")).type(), QNetworkProxy::HttpProxy);
    }

    void reversedExceptions()
    {
        // Only the listed hosts go through the proxy
        writeSettings("ProxyType=1\nhttpsProxy=http://proxy.example.com 3128\nNoProxyFor=graph.microsoft.com\nReversedException=true\n");
        QCOMPARE(proxyFor(QStringLiteral("https://graph.microsoft.com/")).type(), QNetworkProxy::HttpProxy);
        QCOMPARE(proxyFor(QStringLiteral("https://login.microsoftonline.com/")).type(), QNetworkProxy::NoProxy);
    }

    void environment()
    {
        qputenv("GRAPHCORE_TEST_HTTPS_PROXY", "http://envproxy.example.com:3130");
        qputenv("GRAPHCORE_TEST_NO_PROXY", "localhost,.example.net");
        writeSettings("ProxyType=4\nhttpsProxy=GRAPHCORE_TEST_HTTPS_PROXY\nNoProxyFor=GRAPHCORE_TEST_NO_PROXY\n");
        const QNetworkProxy proxy = proxyFor(QStringLiteral("https://graph.microsoft.com/"));
        QCOMPARE(proxy.hostName(), QStringLiteral("envproxy.example.com"));
        QCOMPARE(proxy.port(), quint16(3130));
        QCOMPARE(proxyFor(QStringLiteral("https://www.example.net/")).type(), QNetworkProxy::NoProxy);
    }

    void loopbackNeverProxied()
    {
        writeSettings("ProxyType=1\nhttpsProxy=http://proxy.example.com 3128\nhttpProxy=http://proxy.example.com 3128\n");
        QCOMPARE(proxyFor(QStringLiteral("http://127.0.0.1:43210/")).type(), QNetworkProxy::NoProxy);
        QCOMPARE(proxyFor(QStringLiteral("http://localhost:43210/")).type(), QNetworkProxy::NoProxy);
        QCOMPARE(proxyFor(QStringLiteral("http://[::1]:43210/")).type(), QNetworkProxy::NoProxy);
    }

    void credentials()
    {
        writeSettings("ProxyType=1\nhttpsProxy=http://someone:secret@proxy.example.com 3128\n");
        const QNetworkProxy proxy = proxyFor(QStringLiteral("https://graph.microsoft.com/"));
        QCOMPARE(proxy.user(), QStringLiteral("someone"));
        QCOMPARE(proxy.password(), QStringLiteral("secret"));
    }
};

QTEST_GUILESS_MAIN(ProxyTest)
#include "proxytest.moc"
