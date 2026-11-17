// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "accountserver.h"
#include "accounts.h"
#include "auth.h"
#include "graphaccountsd_debug.h"
#include "graphclient.h"
#include "scopes.h"
#include "serviceclient.h"
#include "signin.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KNotification>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QUuid>
#include <QtConcurrentRun>

namespace GraphCore
{

namespace
{
QString problemName(Auth::Problem problem)
{
    switch (problem) {
    case Auth::Problem::None:
        return {};
    case Auth::Problem::NotSignedIn:
        return QStringLiteral("NotSignedIn");
    case Auth::Problem::SignInExpired:
        return QStringLiteral("SignInExpired");
    case Auth::Problem::ConsentRequired:
        return QStringLiteral("ConsentRequired");
    case Auth::Problem::Offline:
        return QStringLiteral("Offline");
    case Auth::Problem::KeychainLocked:
        return QStringLiteral("KeychainLocked");
    case Auth::Problem::Other:
        break;
    }
    return QStringLiteral("Other");
}

#ifdef GRAPHACCOUNTS_TEST_HOOKS
// Tests only (a separate build, graphaccountsd_test): refresh tokens in memory, "id=token,..."
QHash<QString, QString> testRefreshTokens()
{
    QHash<QString, QString> tokens;
    const QStringList pairs = qEnvironmentVariable("GRAPHACCOUNTS_TEST_REFRESH_TOKENS").split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        tokens.insert(pair.section(QLatin1Char('='), 0, 0), pair.section(QLatin1Char('='), 1));
    }
    return tokens;
}
#endif

struct TokenResult {
    QString token;
    qint64 expiresAtMs = 0;
    Auth::Problem problem = Auth::Problem::None;
    QString error;
    // The app whose stored sign-in was taken over, if this request did that
    QString migratedFrom;
};
}

struct AccountServer::State {
    Account account;
    // One sign-in (refresh token, one keychain entry) for both permission sets: "directory" has
    // admin-consent permissions and its own access token, so a tenant without that consent keeps
    // its "suite" token
    std::shared_ptr<Auth> suite;
    std::shared_ptr<Auth> directory;
    // The apps' stored sign-ins of this account with the same registration: (service, key)
    QList<QPair<QString, QString>> earlier;
    QMutex migrationMutex;
    bool migrationTried = false; // guarded by migrationMutex
    // Main thread only
    QString status = QStringLiteral("unknown");
    // SignInNeeded sent since the last success
    bool announced = false;
    // The apps' own stored sign-ins of this account were removed (step 6 of phase 10): once the
    // service has used its own, the old copies only remain a risk. Main thread only
    bool earlierRemoved = false;
};

QString AccountServer::keychainService()
{
    return QStringLiteral("plasma-graphcore-linux");
}

QStringList AccountServer::earlierApps()
{
    return {QStringLiteral("plasma-graphmail-linux"), QStringLiteral("plasma-graphsync-linux")};
}

AccountServer::AccountServer(QObject *parent)
    : QObject(parent)
{
    const QStringList directories{QCoreApplication::applicationDirPath(),
                                  QStringLiteral(GRAPHCORE_BINDIR),
                                  QStringLiteral("/usr/bin"),
                                  QStringLiteral("/usr/local/bin"),
                                  QDir::homePath() + QStringLiteral("/.local/bin")};
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(Accounts::configFileName(), KConfig::NoGlobals);
    const QStringList extra = config->group(QStringLiteral("Development")).readEntry("AllowedExecutables", QStringList());
    m_allowed = CallerCheck::allowedFor(directories, extra);
    // System Settings only from the system's own places (its Microsoft 365 Accounts page)
    m_managers = CallerCheck::allowedFor({QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")}, {}, CallerCheck::managerNames());
    if (!extra.isEmpty()) {
        qCInfo(GRAPHACCOUNTS) << "Also allowed (graphaccountsrc [Development]):" << extra;
    }
}

AccountServer::~AccountServer()
{
    for (const std::shared_ptr<std::atomic_bool> &cancel : std::as_const(m_signIns)) {
        cancel->store(true);
    }
}

bool AccountServer::registerOnBus(QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // The object first, so a caller that started this service through D-Bus activation finds it
    if (!bus.registerObject(GraphCore::AccountService::objectPath(), this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        *error = QStringLiteral("cannot register the object on the session bus");
        return false;
    }
    if (!bus.registerService(GraphCore::AccountService::serviceName())) {
        *error = QStringLiteral("cannot register %1 on the session bus (already running?)").arg(GraphCore::AccountService::serviceName());
        return false;
    }
    return true;
}

void AccountServer::refuse(const QString &problem, const QString &text)
{
    sendErrorReply(GraphCore::AccountService::errorName(problem), text);
}

bool AccountServer::callerAllowed(bool tokens)
{
    if (!calledFromDBus()) {
        return true;
    }
    const QString sender = message().service();
    int level = m_callers.value(sender, -1);
    if (level < 0) {
        // Unique bus names are never reused, so the answer can be kept
        const QDBusReply<uint> pid = connection().interface()->servicePid(sender);
        const QString executable = pid.isValid() ? CallerCheck::executableOf(pid.value()) : QString();
        level = CallerCheck::isAllowed(executable, m_allowed) ? 2 : CallerCheck::isAllowed(executable, m_managers) ? 1 : 0;
        m_callers.insert(sender, level);
        if (level == 0) {
            qCWarning(GRAPHACCOUNTS).noquote() << "Refused a call from" << (executable.isEmpty() ? sender : executable);
        }
    }
    if (level == 2 || (level == 1 && !tokens)) {
        return true;
    }
    if (level == 1) {
        qCWarning(GRAPHACCOUNTS) << "Refused a token to a program that only manages accounts";
    }
    refuse(QStringLiteral("NotAllowed"), QStringLiteral("only the apps of the suite may use the account service"));
    return false;
}

std::shared_ptr<AccountServer::State> AccountServer::stateFor(const QString &id)
{
    const Account account = GraphCore::Accounts::find(id);
    if (!account.isValid()) {
        m_states.remove(id);
        return nullptr;
    }
    if (const std::shared_ptr<State> known = m_states.value(id)) {
        const Account &was = known->account;
        if (was.tenantId == account.tenantId && was.objectId == account.objectId && was.effectiveClientId() == account.effectiveClientId()) {
            known->account = account;
            return known;
        }
    }
    auto state = std::make_shared<State>();
    state->account = account;
    state->suite = std::make_shared<Auth>(Auth::settingsFor(account, keychainService(), Scopes::suite()));
    state->directory = std::make_shared<Auth>(Auth::settingsFor(account, keychainService(), Scopes::directory()));
    const QString client = account.effectiveClientId();
    for (const QString &app : earlierApps()) {
        // Only a sign-in with this registration can be used here
        if (SignIn::normalizedId(GraphCore::Accounts::clientIdOf(id, app)) == SignIn::normalizedId(client)) {
            state->earlier.append({app, Auth::keychainKeyFor(account.tenantId, client, account.objectId)});
        }
    }
#ifdef GRAPHACCOUNTS_TEST_HOOKS
    if (const QString token = testRefreshTokens().value(id); !token.isEmpty()) {
        state->suite->setRefreshTokenForTesting(token);
        state->directory->setRefreshTokenForTesting(token);
    }
#endif
    m_states.insert(id, state);
    return state;
}

void AccountServer::removeEarlierSignIns(const std::shared_ptr<State> &state)
{
    if (state->earlierRemoved) {
        return;
    }
    state->earlierRemoved = true;
    const QList<QPair<QString, QString>> earlier = state->earlier;
    const QString account = state->account.id;
    // The keychain is slow and may ask: not on the main thread
    (void)QtConcurrent::run([state, earlier, account] {
        QMutexLocker locker(&state->migrationMutex);
        for (const auto &[service, key] : earlier) {
            Auth::removeToken(service, key);
        }
        // Nothing to take over any more
        state->migrationTried = true;
        qCInfo(GRAPHACCOUNTS).noquote() << "Removed the apps' own stored sign-ins of account" << account << "(the account service keeps the one sign-in)";
    });
}

void AccountServer::setStatus(const std::shared_ptr<State> &state, const QString &status, const QString &problem)
{
    const bool changed = state->status != status;
    state->status = status;
    if (status == QLatin1String("signInNeeded") && !state->announced) {
        state->announced = true;
        Q_EMIT SignInNeeded(state->account.id, problem);
        notifySignInNeeded(state, problem);
    } else if (status == QLatin1String("signedIn") || status == QLatin1String("signedOut")) {
        state->announced = false;
        // Signed in again (or signed out on purpose): the notification no longer applies
        if (const QPointer<KNotification> open = m_notifications.take(state->account.id)) {
            open->close();
        }
    }
    if (changed) {
        Q_EMIT AccountsChanged();
    }
}

void AccountServer::notifySignInNeeded(const std::shared_ptr<State> &state, const QString &problem)
{
    // KNotification needs a GUI application; without a display (ssh, tests) the apps' signal is all
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    const Account account = state->account;
    const QString who = account.userPrincipalName.isEmpty() ? account.displayName : account.userPrincipalName;
    auto *notification = new KNotification(QStringLiteral("signInNeeded"), KNotification::Persistent);
    notification->setComponentName(QStringLiteral("plasma-graphcore-linux"));
    if (problem == QLatin1String("ConsentRequired")) {
        notification->setTitle(i18nc("@title", "Approval needed"));
        notification->setText(i18nc("@info %1 an account", "Your organisation's administrator has to approve the Microsoft 365 apps for %1 first. Mail, files and the other apps of the suite wait until then.", who.toHtmlEscaped()));
        KNotificationAction *details = notification->addAction(i18nc("@action:button", "Show Details"));
        connect(details, &KNotificationAction::activated, this, [] {
            QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_graphaccounts")});
        });
    } else {
        notification->setTitle(i18nc("@title", "Sign in again"));
        notification->setText(i18nc("@info %1 an account", "The sign-in of %1 expired. Mail, files and the other apps of the suite wait until you sign in again.", who.toHtmlEscaped()));
        KNotificationAction *signIn = notification->addAction(i18nc("@action:button", "Sign In"));
        connect(signIn, &KNotificationAction::activated, this, [this, account] {
            // The same registration and organisation, the account's address filled in
            const QString client = account.clientId.isEmpty() ? GraphCore::Accounts::defaultClientId() : account.clientId;
            startSignIn(GraphCore::SignIn::normalizedId(client), account.tenantId, account.userPrincipalName, false);
        });
    }
    if (const QPointer<KNotification> old = m_notifications.take(account.id)) {
        old->close();
    }
    m_notifications.insert(account.id, notification);
    notification->sendEvent();
}

QString AccountServer::Accounts()
{
    if (!callerAllowed()) {
        return {};
    }
    QJsonArray all;
    const QList<Account> accounts = GraphCore::Accounts::all();
    for (const Account &account : accounts) {
        const std::shared_ptr<State> state = stateFor(account.id);
        all.append(QJsonObject{{QStringLiteral("id"), account.id},
                               {QStringLiteral("tenantId"), account.tenantId},
                               {QStringLiteral("objectId"), account.objectId},
                               {QStringLiteral("userPrincipalName"), account.userPrincipalName},
                               {QStringLiteral("displayName"), account.displayName},
                               {QStringLiteral("organization"), account.organization},
                               {QStringLiteral("state"), state ? state->status : QStringLiteral("unknown")}});
    }
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

QString AccountServer::AccessToken(const QString &account, const QString &scopeSet, bool forceRefresh, qlonglong &expiresAtMs)
{
    expiresAtMs = 0;
    if (!callerAllowed(true)) {
        return {};
    }
    if (scopeSet != GraphCore::AccountService::suite() && scopeSet != GraphCore::AccountService::directory()) {
        refuse(QStringLiteral("InvalidArgs"), QStringLiteral("unknown permission set: %1").arg(scopeSet));
        return {};
    }
    const std::shared_ptr<State> state = stateFor(account);
    if (!state) {
        refuse(QStringLiteral("UnknownAccount"), QStringLiteral("no such account: %1").arg(account));
        return {};
    }
    setDelayedReply(true);
    const QDBusMessage request = message();
    QDBusConnection bus = connection();
    const std::shared_ptr<Auth> auth = scopeSet == GraphCore::AccountService::directory() ? state->directory : state->suite;
    const QString target = keychainService();
    const QString ownKey = Auth::keychainKeyFor(state->account.tenantId, state->account.effectiveClientId(), state->account.objectId);

    auto *watcher = new QFutureWatcher<TokenResult>(this);
    connect(watcher, &QFutureWatcher<TokenResult>::finished, this, [this, watcher, request, bus, state, scopeSet]() mutable {
        watcher->deleteLater();
        const TokenResult result = watcher->result();
        if (!result.migratedFrom.isEmpty()) {
            qCInfo(GRAPHACCOUNTS).noquote() << "Took over the sign-in of account" << state->account.id << "from" << result.migratedFrom;
        }
        if (!result.token.isEmpty()) {
            bus.send(request.createReply({result.token, result.expiresAtMs}));
            setStatus(state, QStringLiteral("signedIn"), {});
            removeEarlierSignIns(state);
            return;
        }
        const QString problem = problemName(result.problem);
        bus.send(request.createErrorReply(GraphCore::AccountService::errorName(problem), result.error));
        // Missing consent for the directory permissions alone says nothing about the sign-in
        if (scopeSet == GraphCore::AccountService::directory() && result.problem == Auth::Problem::ConsentRequired) {
            return;
        }
        switch (result.problem) {
        case Auth::Problem::SignInExpired:
        case Auth::Problem::ConsentRequired:
        case Auth::Problem::NotSignedIn:
            setStatus(state, QStringLiteral("signInNeeded"), problem);
            break;
        case Auth::Problem::KeychainLocked:
            setStatus(state, QStringLiteral("keychainLocked"), problem);
            break;
        default:
            // Offline and other passing trouble: the sign-in itself is as it was
            break;
        }
    });
    watcher->setFuture(QtConcurrent::run([state, auth, forceRefresh, target, ownKey]() {
        TokenResult result;
        result.token = auth->accessToken(&result.error, forceRefresh, &result.problem);
        if (result.token.isEmpty() && result.problem == Auth::Problem::NotSignedIn) {
            // Never signed in here: take over a sign-in an app stored before the service existed
            // (the apps' entries stay until both apps are switched; removing them is a later step)
            QMutexLocker locker(&state->migrationMutex);
            if (!state->migrationTried) {
                bool locked = false;
                for (const auto &[service, key] : std::as_const(state->earlier)) {
                    QString copyError;
                    const Auth::CopyResult copied = Auth::copyToken(service, key, target, ownKey, false, &copyError);
                    if (copied == Auth::CopyResult::Copied) {
                        result.migratedFrom = service;
                        break;
                    }
                    if (copied == Auth::CopyResult::Failed) {
                        locked = true;
                        result.error = QStringLiteral("the keychain cannot be read: ") + copyError;
                        break;
                    }
                }
                if (locked) {
                    result.problem = Auth::Problem::KeychainLocked;
                    return result;
                }
                state->migrationTried = true;
            }
            if (!result.migratedFrom.isEmpty()) {
                result.token = auth->accessToken(&result.error, forceRefresh, &result.problem);
            }
        }
        if (!result.token.isEmpty()) {
            result.problem = Auth::Problem::None;
            result.expiresAtMs = auth->accessTokenExpiry().toMSecsSinceEpoch();
        }
        return result;
    }));
    return {};
}

QString AccountServer::SignIn(const QString &clientIdOrSetupText, const QString &tenantOrAddress, const QString &loginHint, bool deviceCode)
{
    if (!callerAllowed()) {
        return {};
    }
    QString client;
    QString tenant = tenantOrAddress.trimmed();
    const QString given = clientIdOrSetupText.trimmed();
    const GraphCore::SignIn::SetupText setup = GraphCore::SignIn::parseSetupText(given);
    if (!setup.clientId.isEmpty()) {
        client = setup.clientId;
        if (tenant.isEmpty()) {
            tenant = setup.tenant;
        }
    } else if (GraphCore::SignIn::isGuid(given)) {
        client = given;
    } else if (given.isEmpty()) {
        client = GraphCore::Accounts::defaultClientId();
    } else {
        refuse(QStringLiteral("InvalidArgs"), QStringLiteral("neither a client ID nor a setup text"));
        return {};
    }
    if (client.isEmpty()) {
        refuse(QStringLiteral("InvalidArgs"), QStringLiteral("no app registration: give a client ID (none is configured for the suite)"));
        return {};
    }
    return startSignIn(GraphCore::SignIn::normalizedId(client), tenant, loginHint, deviceCode);
}

QString AccountServer::startSignIn(const QString &client, const QString &tenant, const QString &loginHint, bool deviceCode)
{
    const QString request = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_signIns.insert(request, cancel);
    const QString service = keychainService();
    // A sign-in blocks (browser up to five minutes, a device code up to fifteen): a thread of its own
    QThread *thread = QThread::create([this, request, client, tenant, loginHint, deviceCode, cancel, service] {
        QString resolved = QStringLiteral("organizations");
        QString error;
        bool ok = false;
        if (!tenant.isEmpty()) {
            bool offline = false;
            resolved = GraphCore::SignIn::resolveTenant(tenant, &offline);
            if (resolved.isEmpty()) {
                error = offline ? QStringLiteral("no connection to look up the organisation") : QStringLiteral("no organisation found for %1").arg(tenant);
            }
        }
        Auth auth({service, client, resolved, {}, Scopes::suite()});
        if (error.isEmpty()) {
            if (deviceCode) {
                ok = auth.deviceCodeLogin(
                    [this, request](const QString &code, const QString &address) {
                        QMetaObject::invokeMethod(this, [this, request, code, address] {
                            Q_EMIT SignInProgress(request, code, address);
                        });
                    },
                    cancel.get(),
                    &error);
            } else {
                ok = auth.interactiveLogin(&error, loginHint);
            }
        }
        const SignedInUser user = ok ? auth.signedInUser() : SignedInUser{};
        // The organisation's name for the account list (kept from before when this fails)
        QString organization;
        if (ok) {
            GraphClient graph(&auth);
            const GraphResponse answer = graph.get(QStringLiteral("/organization?$select=displayName"));
            if (answer.ok()) {
                organization = answer.json().value(QLatin1String("value")).toArray().first().toObject().value(QLatin1String("displayName")).toString();
            }
        }
        QMetaObject::invokeMethod(this, [this, request, client, user, organization, ok, error] {
            finishSignIn(request, client, user, organization, ok, error);
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return request;
}

void AccountServer::finishSignIn(const QString &request, const QString &clientId, const SignedInUser &user, const QString &organization, bool ok, const QString &error)
{
    m_signIns.remove(request);
    if (!ok) {
        qCInfo(GRAPHACCOUNTS).noquote() << "A sign-in did not complete:" << error;
        Q_EMIT SignInFinished(request, {}, error);
        return;
    }
    Account account = Auth::accountFor(user, clientId);
    account.organization = organization;
    if (organization.isEmpty()) {
        account.organization = GraphCore::Accounts::findUser(user.tenantId, user.objectId).organization;
    }
    const Account stored = GraphCore::Accounts::add(account);
    // A fresh state reads the new sign-in
    m_states.remove(stored.id);
    if (const std::shared_ptr<State> state = stateFor(stored.id)) {
        QMutexLocker locker(&state->migrationMutex);
        state->migrationTried = true;
        locker.unlock();
        setStatus(state, QStringLiteral("signedIn"), {});
    }
    qCInfo(GRAPHACCOUNTS).noquote() << "Signed in account" << stored.id;
    Q_EMIT AccountsChanged();
    Q_EMIT SignInFinished(request, stored.id, {});
}

void AccountServer::CancelSignIn(const QString &request)
{
    if (!callerAllowed()) {
        return;
    }
    if (const std::shared_ptr<std::atomic_bool> cancel = m_signIns.value(request)) {
        cancel->store(true);
    }
}

void AccountServer::SignOut(const QString &account)
{
    if (!callerAllowed()) {
        return;
    }
    const std::shared_ptr<State> state = stateFor(account);
    if (!state) {
        refuse(QStringLiteral("UnknownAccount"), QStringLiteral("no such account: %1").arg(account));
        return;
    }
    setDelayedReply(true);
    const QDBusMessage request = message();
    QDBusConnection bus = connection();
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, request, bus, state]() mutable {
        watcher->deleteLater();
        bus.send(request.createReply());
        qCInfo(GRAPHACCOUNTS).noquote() << "Signed out account" << state->account.id;
        setStatus(state, QStringLiteral("signedOut"), {});
    });
    watcher->setFuture(QtConcurrent::run([state] {
        state->suite->logout();
        state->directory->logout();
        // The apps' own stored sign-ins too: else the next request would take one over again
        QMutexLocker locker(&state->migrationMutex);
        for (const auto &[service, key] : std::as_const(state->earlier)) {
            Auth::removeToken(service, key);
        }
        state->migrationTried = true;
    }));
}

}
