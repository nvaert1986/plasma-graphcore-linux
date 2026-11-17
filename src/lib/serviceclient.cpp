// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serviceclient.h"

#include <QProcess>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QTimeZone>

#include <atomic>
#include <mutex>

namespace GraphCore
{

namespace
{
// A refresh may take a while (keychain, network with retries); never block for ever
constexpr int tokenTimeoutMs = 120 * 1000;
constexpr int shortTimeoutMs = 25 * 1000;
constexpr int refreshMarginSeconds = 300;

QDBusMessage call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(AccountService::serviceName(), AccountService::objectPath(), AccountService::interfaceName(), method);
    message.setArguments(arguments);
    return message;
}

// Counts the service's AccountsChanged signals in this process: a kept token is only used while the
// count is the one it was fetched under, so a sign-out (or a new sign-in) reaches every app at once
std::atomic<quint64> accountsGeneration{1};

class GenerationWatcher : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    void bump()
    {
        accountsGeneration.fetch_add(1);
    }
};

void watchAccountChanges()
{
    static std::once_flag once;
    std::call_once(once, [] {
        QCoreApplication *app = QCoreApplication::instance();
        if (!app) {
            return;
        }
        // Lives on the main thread, where the bus delivers the signal
        auto *watcher = new GenerationWatcher;
        watcher->moveToThread(app->thread());
        watcher->setParent(app);
        QDBusConnection::sessionBus().connect(AccountService::serviceName(), AccountService::objectPath(), AccountService::interfaceName(), QStringLiteral("AccountsChanged"),
                                              watcher, SLOT(bump()));
    });
}
}

namespace AccountService
{
QString serviceName()
{
    return QStringLiteral("io.github.nvaert1986.GraphAccounts");
}

QString objectPath()
{
    return QStringLiteral("/Accounts");
}

QString interfaceName()
{
    return QStringLiteral("io.github.nvaert1986.GraphAccounts1");
}

QString errorName(const QString &problem)
{
    return interfaceName() + QStringLiteral(".Error.") + problem;
}

QString problemOf(const QString &errorName)
{
    const QString prefix = interfaceName() + QStringLiteral(".Error.");
    return errorName.startsWith(prefix) ? errorName.mid(prefix.size()) : QString();
}

bool openSettings()
{
    const QStringList page{QStringLiteral("kcm_graphaccounts")};
    return QProcess::startDetached(QStringLiteral("systemsettings"), page) || QProcess::startDetached(QStringLiteral("kcmshell6"), page);
}
}

ServiceTokenProvider::ServiceTokenProvider(const QString &accountId, const QString &scopeSet)
    : m_account(accountId)
    , m_scopeSet(scopeSet)
{
    watchAccountChanges();
}

QString ServiceTokenProvider::accountId() const
{
    return m_account;
}

QString ServiceTokenProvider::cachedAccessToken()
{
    QMutexLocker locker(&m_mutex);
    const bool fresh = !m_token.isEmpty() && m_expiresAt.isValid() && QDateTime::currentDateTimeUtc().secsTo(m_expiresAt) > refreshMarginSeconds
        && m_generation == accountsGeneration.load();
    return fresh ? m_token : QString();
}

QString ServiceTokenProvider::accessToken(QString *error, bool forceRefresh)
{
    if (!forceRefresh) {
        if (QString token = cachedAccessToken(); !token.isEmpty()) {
            return token;
        }
    }
    // The count before asking: a change while the call runs makes the next request ask again
    const quint64 generation = accountsGeneration.load();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call(QStringLiteral("AccessToken"), {m_account, m_scopeSet, forceRefresh}), QDBus::Block, tokenTimeoutMs);
    QMutexLocker locker(&m_mutex);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) {
        m_lastProblem = AccountService::problemOf(reply.errorName());
        if (error) {
            *error = reply.errorMessage().isEmpty() ? QStringLiteral("the account service did not answer") : reply.errorMessage();
        }
        m_token.clear();
        return {};
    }
    m_lastProblem.clear();
    m_generation = generation;
    m_token = reply.arguments().at(0).toString();
    m_expiresAt = QDateTime::fromMSecsSinceEpoch(reply.arguments().at(1).toLongLong(), QTimeZone::UTC);
    return m_token;
}

QString ServiceTokenProvider::lastProblem() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastProblem;
}

AccountsClient::AccountsClient(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = AccountService::serviceName();
    const QString path = AccountService::objectPath();
    const QString iface = AccountService::interfaceName();
    bus.connect(service, path, iface, QStringLiteral("AccountsChanged"), this, SLOT(onAccountsChanged()));
    bus.connect(service, path, iface, QStringLiteral("SignInNeeded"), this, SLOT(onSignInNeeded(QString, QString)));
    bus.connect(service, path, iface, QStringLiteral("SignInProgress"), this, SLOT(onSignInProgress(QString, QString, QString)));
    bus.connect(service, path, iface, QStringLiteral("SignInFinished"), this, SLOT(onSignInFinished(QString, QString, QString)));
}

QList<ServiceAccount> AccountsClient::accounts(QString *error) const
{
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call(QStringLiteral("Accounts"), {}), QDBus::Block, shortTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (error) {
            *error = reply.errorMessage();
        }
        return {};
    }
    QList<ServiceAccount> result;
    const QJsonArray all = QJsonDocument::fromJson(reply.arguments().constFirst().toString().toUtf8()).array();
    for (const QJsonValue &value : all) {
        const QJsonObject o = value.toObject();
        result.append({o.value(QLatin1String("id")).toString(),
                       o.value(QLatin1String("tenantId")).toString(),
                       o.value(QLatin1String("objectId")).toString(),
                       o.value(QLatin1String("userPrincipalName")).toString(),
                       o.value(QLatin1String("displayName")).toString(),
                       o.value(QLatin1String("organization")).toString(),
                       o.value(QLatin1String("state")).toString()});
    }
    return result;
}

QString AccountsClient::signIn(const QString &clientIdOrSetupText, const QString &tenantOrAddress, const QString &loginHint, bool deviceCode, QString *error)
{
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call(QStringLiteral("SignIn"), {clientIdOrSetupText, tenantOrAddress, loginHint, deviceCode}), QDBus::Block, shortTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (error) {
            *error = reply.errorMessage();
        }
        return {};
    }
    return reply.arguments().constFirst().toString();
}

bool AccountsClient::cancelSignIn(const QString &request)
{
    return QDBusConnection::sessionBus().call(call(QStringLiteral("CancelSignIn"), {request}), QDBus::Block, shortTimeoutMs).type() == QDBusMessage::ReplyMessage;
}

bool AccountsClient::signOut(const QString &accountId, QString *error)
{
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call(QStringLiteral("SignOut"), {accountId}), QDBus::Block, tokenTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        if (error) {
            *error = reply.errorMessage();
        }
        return false;
    }
    return true;
}

void AccountsClient::onAccountsChanged()
{
    Q_EMIT accountsChanged();
}

void AccountsClient::onSignInNeeded(const QString &account, const QString &reason)
{
    Q_EMIT signInNeeded(account, reason);
}

void AccountsClient::onSignInProgress(const QString &request, const QString &code, const QString &address)
{
    Q_EMIT signInProgress(request, code, address);
}

void AccountsClient::onSignInFinished(const QString &request, const QString &account, const QString &error)
{
    Q_EMIT signInFinished(request, account, error);
}

}

#include "serviceclient.moc"
