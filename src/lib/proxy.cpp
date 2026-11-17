// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "proxy.h"
#include "graphcore_debug.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDateTime>
#include <QFileInfo>
#include <QHostAddress>
#include <QMutex>
#include <QNetworkProxyFactory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace GraphCore::Proxy
{

namespace
{
// KDE's proxy types (kioslaverc [Proxy Settings] ProxyType)
enum Type { None = 0, Manual = 1, Script = 2, Automatic = 3, Environment = 4 };

struct Settings {
    int type = None;
    QString https;
    QString http;
    QString socks;
    QStringList exceptions;
    bool reversed = false;
};

QMutex mutex;
Settings cached;
QString cachedFile;
QDateTime cachedTime;
bool loaded = false;

// The settings, read again when kioslaverc changed
Settings settings()
{
    QMutexLocker lock(&mutex);
    const QString file = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kioslaverc"));
    const QDateTime time = file.isEmpty() ? QDateTime() : QFileInfo(file).lastModified();
    if (loaded && file == cachedFile && time == cachedTime) {
        return cached;
    }
    Settings s;
    if (!file.isEmpty()) {
        const KConfig config(file, KConfig::SimpleConfig);
        const KConfigGroup group = config.group(QStringLiteral("Proxy Settings"));
        s.type = group.readEntry("ProxyType", int(None));
        s.https = group.readEntry("httpsProxy").trimmed();
        s.http = group.readEntry("httpProxy").trimmed();
        s.socks = group.readEntry("socksProxy").trimmed();
        QString noProxy = group.readEntry("NoProxyFor").trimmed();
        if (s.type == Environment) {
            // The entries name environment variables here
            const auto fromEnv = [](const QString &name) {
                return name.isEmpty() ? QString() : qEnvironmentVariable(name.toLocal8Bit().constData()).trimmed();
            };
            s.https = fromEnv(s.https);
            s.http = fromEnv(s.http);
            s.socks = fromEnv(s.socks);
            noProxy = fromEnv(noProxy);
        }
        s.exceptions = noProxy.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        s.reversed = group.readEntry("ReversedException", false);
    }
    if (!loaded || s.type != cached.type) {
        qCInfo(GRAPHCORE) << "System proxy setting:" << s.type;
    }
    cached = s;
    cachedFile = file;
    cachedTime = time;
    loaded = true;
    return s;
}

// "http://host 8080" (KDE's form), "http://host:8080", "host:8080"; NoProxy when not usable
QNetworkProxy parse(const QString &value, QNetworkProxy::ProxyType defaultType)
{
    if (value.isEmpty()) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    QString text = value;
    int port = -1;
    const int space = text.lastIndexOf(QLatin1Char(' '));
    if (space > 0) {
        bool ok = false;
        port = text.mid(space + 1).toInt(&ok);
        if (!ok) {
            port = -1;
        }
        text = text.left(space).trimmed();
    }
    if (!text.contains(QLatin1String("://"))) {
        text.prepend(defaultType == QNetworkProxy::Socks5Proxy ? QStringLiteral("socks://") : QStringLiteral("http://"));
    }
    const QUrl url(text);
    if (!url.isValid() || url.host().isEmpty()) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    const QString scheme = url.scheme().toLower();
    const QNetworkProxy::ProxyType type = scheme.startsWith(QLatin1String("socks")) ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy;
    if (port < 0) {
        port = url.port(type == QNetworkProxy::Socks5Proxy ? 1080 : 8080);
    }
    return QNetworkProxy(type, url.host(), quint16(port), url.userName(), url.password());
}

// KDE's exception list: host names (also matching their subdomains), "*.domain", ".domain",
// addresses and subnets ("10.0.0.0/8"), "<local>" for names without a dot
bool isException(const QString &host, const QStringList &exceptions)
{
    const QString name = host.toLower();
    const QHostAddress address(name);
    for (QString entry : exceptions) {
        entry = entry.toLower();
        if (entry == QLatin1String("<local>")) {
            if (!name.contains(QLatin1Char('.'))) {
                return true;
            }
            continue;
        }
        if (entry.contains(QLatin1Char('/')) && !address.isNull()) {
            const std::pair<QHostAddress, int> subnet = QHostAddress::parseSubnet(entry);
            if (!subnet.first.isNull() && address.isInSubnet(subnet)) {
                return true;
            }
            continue;
        }
        if (entry.startsWith(QLatin1String("*."))) {
            entry.remove(0, 1);
        }
        if (entry.startsWith(QLatin1Char('.'))) {
            if (name.endsWith(entry) || name == entry.mid(1)) {
                return true;
            }
            continue;
        }
        if (name == entry || name.endsWith(QLatin1Char('.') + entry)) {
            return true;
        }
    }
    return false;
}

class Factory : public QNetworkProxyFactory
{
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
    {
        return proxiesFor(query);
    }
};
}

QList<QNetworkProxy> proxiesFor(const QNetworkProxyQuery &query)
{
    // This computer itself never goes through a proxy (a sign-in's loopback redirect, test servers)
    const QString peer = query.peerHostName().toLower();
    if (peer == QLatin1String("localhost") || QHostAddress(peer).isLoopback()) {
        return {QNetworkProxy(QNetworkProxy::NoProxy)};
    }
    const Settings s = settings();
    switch (s.type) {
    case Script:
    case Automatic:
        // A PAC script or WPAD: Qt's own system lookup (libproxy where Qt has it, else the environment)
        return QNetworkProxyFactory::systemProxyForQuery(query);
    case Manual:
    case Environment: {
        const QString host = query.peerHostName();
        if (!host.isEmpty() && isException(host, s.exceptions) != s.reversed) {
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        }
        const QString scheme = query.protocolTag().toLower();
        QNetworkProxy proxy = parse(scheme == QLatin1String("http") ? s.http : s.https, QNetworkProxy::HttpProxy);
        if (proxy.type() == QNetworkProxy::NoProxy && scheme != QLatin1String("http")) {
            // HTTPS through the HTTP proxy is what most set up (CONNECT)
            proxy = parse(s.http, QNetworkProxy::HttpProxy);
        }
        if (proxy.type() == QNetworkProxy::NoProxy) {
            proxy = parse(s.socks, QNetworkProxy::Socks5Proxy);
        }
        return {proxy};
    }
    default:
        return {QNetworkProxy(QNetworkProxy::NoProxy)};
    }
}

void useSystemSettings()
{
    QNetworkProxyFactory::setApplicationProxyFactory(new Factory);
}

}
