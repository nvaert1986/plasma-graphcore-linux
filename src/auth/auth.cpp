// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "auth.h"
#include "graphcore_debug.h"
#include "signin.h"

#include <qt6keychain/keychain.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

#include <functional>
#include <memory>

namespace GraphCore
{

namespace
{
constexpr int loginTimeoutMs = 5 * 60 * 1000;
// An access token this close to expiry is refreshed first
constexpr int refreshMarginSeconds = 300;

// Blocking form POST to the identity platform; the JSON answer (also for errors)
QJsonObject postForm(const QUrl &url, const QUrlQuery &form, QString *networkError)
{
    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(60 * 1000);
    QNetworkReply *reply = nam.post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
    if (networkError) {
        *networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    }
    return QJsonDocument::fromJson(reply->readAll()).object();
}

QJsonObject jwtClaims(const QString &jwt)
{
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return {};
    }
    const QByteArray payload = QByteArray::fromBase64(parts.at(1).toLatin1(), QByteArray::Base64UrlEncoding);
    return QJsonDocument::fromJson(payload).object();
}

// The first line of the identity platform's error description (the rest is trace and time stamps)
QString firstLine(const QJsonObject &answer)
{
    return answer.value(QLatin1String("error_description")).toString().section(QLatin1Char('\n'), 0, 0);
}

// Tests only (setLoginServerForTesting): a loopback server answering in place of Microsoft
QString &testLoginServer()
{
    static QString server;
    return server;
}

// QtKeychain jobs run on one thread of their own, one after the other: the secret service backend
// answers on the thread that first used it, so jobs started on the token refresh's pool threads
// got their answers on another thread and the daemon crashed as soon as two accounts were signed in
// (2026-11-11: "Cannot create children for a parent that is in a different thread", then a crash)
QObject *keychainContext()
{
    static QObject *context = [] {
        auto *thread = new QThread;
        thread->setObjectName(QStringLiteral("keychain"));
        thread->start();
        auto *object = new QObject;
        object->moveToThread(thread);
        return object;
    }();
    return context;
}

// Runs `work` on the keychain thread and waits for it
void onKeychainThread(const std::function<void()> &work)
{
    QObject *context = keychainContext();
    if (QThread::currentThread() == context->thread()) {
        work();
        return;
    }
    QMetaObject::invokeMethod(context, work, Qt::BlockingQueuedConnection);
}

// A QtKeychain job to completion, in a local event loop on the keychain thread
template<typename Job>
bool runKeychainJob(Job *job, QString *error, QKeychain::Error *code = nullptr)
{
    QEventLoop loop;
    QObject::connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job->setAutoDelete(false);
    job->start();
    loop.exec();
    const bool ok = job->error() == QKeychain::NoError;
    if (!ok && error) {
        *error = job->errorString();
    }
    if (code) {
        *code = job->error();
    }
    return ok;
}

// The three operations, each job made, run and gone on the keychain thread
bool keychainRead(const QString &service, const QString &key, QString *data, QString *error, QKeychain::Error *code = nullptr)
{
    bool ok = false;
    QString failure;
    onKeychainThread([&] {
        QKeychain::ReadPasswordJob job(service);
        job.setKey(key);
        ok = runKeychainJob(&job, &failure, code);
        if (ok) {
            *data = job.textData();
        }
    });
    if (!ok && error) {
        *error = failure;
    }
    return ok;
}

bool keychainWrite(const QString &service, const QString &key, const QString &data, QString *error)
{
    bool ok = false;
    QString failure;
    onKeychainThread([&] {
        QKeychain::WritePasswordJob job(service);
        job.setKey(key);
        job.setTextData(data);
        ok = runKeychainJob(&job, &failure);
    });
    if (!ok && error) {
        *error = failure;
    }
    return ok;
}

void keychainDelete(const QString &service, const QString &key)
{
    onKeychainThread([&] {
        QKeychain::DeletePasswordJob job(service);
        job.setKey(key);
        QString ignored;
        runKeychainJob(&job, &ignored);
    });
}

// Parts of a keychain key "tenant:client:user": none may hold the separator, and a stored sign-in
// always belongs to a user
bool validKeyParts(const QString &tenant, const QString &clientId, const QString &objectId)
{
    return SignIn::isGuid(clientId) && !tenant.isEmpty() && !tenant.contains(QLatin1Char(':')) && (objectId.isEmpty() || SignIn::isGuid(objectId));
}
}

Auth::Auth(const AuthSettings &settings)
    : m_settings(settings)
{
}

AuthSettings Auth::settingsFor(const Account &account, const QString &keychainService, const QStringList &scopes)
{
    return {keychainService, account.effectiveClientId(), account.tenantId, account.objectId, scopes};
}

Account Auth::accountFor(const SignedInUser &user, const QString &clientId)
{
    Account account;
    account.tenantId = user.tenantId;
    account.objectId = user.objectId;
    account.userPrincipalName = user.userPrincipalName;
    account.displayName = user.displayName;
    // Only remember a client ID that differs from the suite's default registration
    if (clientId != Accounts::defaultClientId()) {
        account.clientId = clientId;
    }
    return account;
}

bool Auth::isConfigured() const
{
    QMutexLocker locker(&m_refreshMutex);
    return validKeyParts(m_settings.tenant, m_settings.clientId, m_settings.objectId);
}

AuthSettings Auth::settings() const
{
    QMutexLocker locker(&m_refreshMutex);
    return m_settings;
}

SignedInUser Auth::signedInUser() const
{
    QMutexLocker locker(&m_refreshMutex);
    return m_user;
}

void Auth::setLoginServerForTesting(const QUrl &server)
{
    const bool loopback = server.scheme() == QLatin1String("http") && (server.host() == QLatin1String("127.0.0.1") || server.host() == QLatin1String("localhost"));
    testLoginServer() = loopback ? server.toString(QUrl::StripTrailingSlash) : QString();
}

void Auth::setRefreshTokenForTesting(const QString &token)
{
    QMutexLocker locker(&m_refreshMutex);
    m_refreshToken = token;
}

QString Auth::authority() const
{
    const QString server = testLoginServer().isEmpty() ? QStringLiteral("https://login.microsoftonline.com") : testLoginServer();
    return server + QStringLiteral("/%1/oauth2/v2.0").arg(m_settings.tenant);
}

QString Auth::scopeString() const
{
    QStringList scopes = m_settings.scopes;
    // Without these there is no refresh token or ID token
    for (const QString &required : {QStringLiteral("offline_access"), QStringLiteral("openid"), QStringLiteral("profile")}) {
        if (!scopes.contains(required)) {
            scopes.append(required);
        }
    }
    return scopes.join(QLatin1Char(' '));
}

QString Auth::keychainKeyFor(const QString &tenant, const QString &clientId, const QString &objectId)
{
    return tenant + QLatin1Char(':') + clientId + QLatin1Char(':') + objectId;
}

QString Auth::keychainKey() const
{
    return keychainKeyFor(m_settings.tenant, m_settings.clientId, m_settings.objectId);
}

void Auth::setToken(const QString &token, const QDateTime &expiresAt)
{
    QMutexLocker locker(&m_tokenMutex);
    m_accessToken = token;
    m_expiresAt = expiresAt;
}

bool Auth::completeSignIn(const QString &accessToken, qint64 expiresInSeconds, const QString &refreshToken, const QString &idToken, QString *error)
{
    // Nothing changes until the new user is known and its sign-in is stored: on any failure this
    // Auth keeps what it had
    const SignedInUser user = parseIdToken(idToken);
    if (user.objectId.isEmpty()) {
        // A token without its user could end up with another account
        *error = QStringLiteral("Microsoft did not say which user signed in (no usable ID token); try again");
        return false;
    }
    if (refreshToken.isEmpty()) {
        *error = QStringLiteral("no refresh token in the answer");
        return false;
    }
    // From now on the user's own tenant, also when the sign-in started at "organizations"
    AuthSettings target = m_settings;
    target.tenant = user.tenantId;
    target.objectId = user.objectId;
    if (!writeRefreshTokenFor(target, refreshToken, error)) {
        return false;
    }
    m_settings = target;
    m_user = user;
    m_refreshToken = refreshToken;
    setToken(accessToken, QDateTime::currentDateTimeUtc().addSecs(expiresInSeconds));
    return true;
}

bool Auth::interactiveLogin(QString *error, const QString &loginHint)
{
    QMutexLocker locker(&m_refreshMutex);
    if (!validKeyParts(m_settings.tenant, m_settings.clientId, {})) {
        *error = QStringLiteral("no app registration (client ID) or tenant configured");
        return false;
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
    Q_UNUSED(loginHint)
    *error = QStringLiteral("signing in through the browser needs Qt 6.9 or newer; sign in with a code instead");
    return false;
#else
    QOAuth2AuthorizationCodeFlow flow;
    flow.setAuthorizationUrl(QUrl(authority() + QStringLiteral("/authorize")));
    flow.setTokenUrl(QUrl(authority() + QStringLiteral("/token")));
    flow.setClientIdentifier(m_settings.clientId);
    QSet<QByteArray> scopeTokens;
    const QStringList scopes = scopeString().split(QLatin1Char(' '));
    for (const QString &scope : scopes) {
        scopeTokens.insert(scope.toLatin1());
    }
    flow.setRequestedScopeTokens(scopeTokens);
    flow.setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    flow.setModifyParametersFunction([loginHint](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
            parameters->insert(QStringLiteral("prompt"), QStringLiteral("select_account"));
            if (!loginHint.isEmpty()) {
                parameters->insert(QStringLiteral("login_hint"), loginHint);
            }
        }
    });

    // Entra ignores the port for http://localhost redirect URIs of public clients, so any
    // free port works; it must be "localhost" because that is what the app registration lists.
    auto *handler = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, &flow);
    handler->setCallbackHost(QStringLiteral("localhost"));
    if (!handler->isListening()) {
        *error = QStringLiteral("could not open a local port for the sign-in redirect");
        return false;
    }
    flow.setReplyHandler(handler);

    QEventLoop loop;
    QString failure;
    QObject::connect(&flow, &QAbstractOAuth::authorizeWithBrowser, &flow, [](const QUrl &url) {
        if (!QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString(QUrl::FullyEncoded)})) {
            qCWarning(GRAPHCORE) << "Could not start xdg-open; open this URL manually:" << url.toString(QUrl::FullyEncoded);
        }
    });
    QObject::connect(&flow, &QAbstractOAuth::granted, &loop, &QEventLoop::quit);
    QObject::connect(&flow, &QAbstractOAuth2::serverReportedErrorOccurred, &loop, [&](const QString &code, const QString &description, const QUrl &) {
        failure = code + QStringLiteral(": ") + description;
        loop.quit();
    });
    QObject::connect(&flow, &QAbstractOAuth::requestFailed, &loop, [&](QAbstractOAuth::Error e) {
        failure = QStringLiteral("sign-in request failed (QAbstractOAuth::Error %1)").arg(static_cast<int>(e));
        loop.quit();
    });
    QTimer::singleShot(loginTimeoutMs, &loop, [&] {
        failure = QStringLiteral("no sign-in completed within five minutes");
        loop.quit();
    });

    flow.grant();
    loop.exec();

    if (flow.status() != QAbstractOAuth::Status::Granted) {
        *error = failure.isEmpty() ? QStringLiteral("sign-in did not complete") : failure;
        return false;
    }
    const qint64 expiresIn = std::max<qint64>(60, QDateTime::currentDateTimeUtc().secsTo(flow.expirationAt()));
    return completeSignIn(flow.token(), expiresIn, flow.refreshToken(), flow.idToken(), error);
#endif
}

bool Auth::deviceCodeLogin(const std::function<void(const QString &, const QString &)> &show, const std::atomic_bool *cancel, QString *error)
{
    QMutexLocker locker(&m_refreshMutex);
    if (!validKeyParts(m_settings.tenant, m_settings.clientId, {})) {
        *error = QStringLiteral("no app registration (client ID) or tenant configured");
        return false;
    }
    QUrlQuery request;
    request.addQueryItem(QStringLiteral("client_id"), m_settings.clientId);
    request.addQueryItem(QStringLiteral("scope"), scopeString());
    QString networkError;
    const QJsonObject code = postForm(QUrl(authority() + QStringLiteral("/devicecode")), request, &networkError);
    const QString deviceCode = code.value(QLatin1String("device_code")).toString();
    if (deviceCode.isEmpty()) {
        const QString description = firstLine(code);
        *error = description.isEmpty() ? networkError : description;
        return false;
    }
    show(code.value(QLatin1String("user_code")).toString(), code.value(QLatin1String("verification_uri")).toString());

    int intervalSeconds = std::max(1, code.value(QLatin1String("interval")).toInt(5));
    QElapsedTimer age;
    age.start();
    const qint64 lifetimeMs = qint64(code.value(QLatin1String("expires_in")).toInt(900)) * 1000;
    while (age.elapsed() < lifetimeMs) {
        // Wait in small steps so cancelling is quick
        for (int waited = 0; waited < intervalSeconds * 1000; waited += 250) {
            if (cancel && cancel->load()) {
                *error = QStringLiteral("cancelled");
                return false;
            }
            QEventLoop pause;
            QTimer::singleShot(250, &pause, &QEventLoop::quit);
            pause.exec();
        }
        QUrlQuery poll;
        poll.addQueryItem(QStringLiteral("client_id"), m_settings.clientId);
        poll.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
        poll.addQueryItem(QStringLiteral("device_code"), deviceCode);
        const QJsonObject answer = postForm(QUrl(authority() + QStringLiteral("/token")), poll, &networkError);
        if (answer.contains(QLatin1String("access_token"))) {
            return completeSignIn(answer.value(QLatin1String("access_token")).toString(),
                                  answer.value(QLatin1String("expires_in")).toInt(3600),
                                  answer.value(QLatin1String("refresh_token")).toString(),
                                  answer.value(QLatin1String("id_token")).toString(),
                                  error);
        }
        const QString state = answer.value(QLatin1String("error")).toString();
        if (state == QLatin1String("authorization_pending")) {
            continue;
        }
        if (state == QLatin1String("slow_down")) {
            intervalSeconds += 5;
            continue;
        }
        if (state.isEmpty() && !networkError.isEmpty()) {
            // A network hiccup while waiting; try again
            continue;
        }
        const QString description = firstLine(answer);
        *error = description.isEmpty() ? state : description;
        return false;
    }
    *error = QStringLiteral("the code expired before sign-in completed");
    return false;
}

QString Auth::cachedAccessToken()
{
    QMutexLocker locker(&m_tokenMutex);
    const bool fresh = !m_accessToken.isEmpty() && m_expiresAt.isValid()
        && QDateTime::currentDateTimeUtc().secsTo(m_expiresAt) > refreshMarginSeconds;
    return fresh ? m_accessToken : QString();
}

QString Auth::accessToken(QString *error, bool forceRefresh)
{
    return accessToken(error, forceRefresh, nullptr);
}

QString Auth::accessToken(QString *error, bool forceRefresh, Problem *problem)
{
    if (problem) {
        *problem = Problem::None;
    }
    if (!forceRefresh) {
        if (QString token = cachedAccessToken(); !token.isEmpty()) {
            return token;
        }
    }
    QMutexLocker locker(&m_refreshMutex);
    // Another thread may have refreshed while this one waited for the lock
    if (!forceRefresh) {
        if (QString token = cachedAccessToken(); !token.isEmpty()) {
            return token;
        }
    }
    QString failure;
    Problem why = Problem::None;
    if (!refresh(&failure, &why)) {
        if (error) {
            *error = failure;
        }
        if (problem) {
            *problem = why;
        }
        return {};
    }
    QMutexLocker tokenLocker(&m_tokenMutex);
    return m_accessToken;
}

bool Auth::refresh(QString *error, Problem *problem)
{
    if (m_refreshToken.isEmpty()) {
        bool notFound = false;
        QString readError;
        m_refreshToken = readRefreshToken(&readError, &notFound);
        if (m_refreshToken.isEmpty()) {
            // Not "no such entry" but no answer at all (the wallet not open yet at login, access
            // refused): the sign-in is unknown, not gone
            const bool locked = !notFound && !readError.isEmpty() && !m_settings.objectId.isEmpty();
            *problem = locked ? Problem::KeychainLocked : Problem::NotSignedIn;
            *error = locked ? QStringLiteral("the keychain cannot be read: ") + readError : QStringLiteral("not signed in");
            return false;
        }
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_settings.clientId);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    form.addQueryItem(QStringLiteral("scope"), scopeString());

    QString networkError;
    const QJsonObject body = postForm(QUrl(authority() + QStringLiteral("/token")), form, &networkError);
    if (!body.contains(QLatin1String("access_token"))) {
        const QString code = body.value(QLatin1String("error")).toString();
        const QString description = firstLine(body);
        // Only "sign-in expired" and missing consent need the user; no answer at all (offline,
        // no DNS) is a connection problem that goes away by itself
        if (SignIn::needsAdminConsent(description) || code == QLatin1String("consent_required")) {
            // The refresh token is fine: only these permissions are not approved (Graph Mail's
            // lesson: AADSTS65001 comes as invalid_grant, and must not cost the whole sign-in)
            *problem = Problem::ConsentRequired;
            *error = QStringLiteral("consent required: ") + description;
        } else if (code == QLatin1String("invalid_grant")) {
            // The refresh token is dead (revoked, expired, password change)
            m_refreshToken.clear();
            *problem = Problem::SignInExpired;
            *error = QStringLiteral("sign-in expired: ") + description;
        } else if (body.isEmpty()) {
            *problem = Problem::Offline;
            *error = QStringLiteral("no connection: ") + networkError;
        } else {
            *problem = Problem::Other;
            *error = description.isEmpty() ? code : description;
        }
        return false;
    }

    setToken(body.value(QLatin1String("access_token")).toString(),
             QDateTime::currentDateTimeUtc().addSecs(body.value(QLatin1String("expires_in")).toInt(3600)));
    const QString rotated = body.value(QLatin1String("refresh_token")).toString();
    if (!rotated.isEmpty() && rotated != m_refreshToken) {
        m_refreshToken = rotated;
        QString writeError;
        if (!writeRefreshTokenFor(m_settings, rotated, &writeError)) {
            qCWarning(GRAPHCORE) << "Could not store the rotated refresh token:" << writeError;
        }
    }
    return true;
}

bool Auth::hasStoredRefreshToken()
{
    return storedRefreshToken() == StoredToken::Present;
}

Auth::StoredToken Auth::storedRefreshToken()
{
    QMutexLocker locker(&m_refreshMutex);
    if (!m_refreshToken.isEmpty()) {
        return StoredToken::Present;
    }
    QString error;
    bool notFound = false;
    m_refreshToken = readRefreshToken(&error, &notFound);
    if (!m_refreshToken.isEmpty()) {
        return StoredToken::Present;
    }
    // No object id: never signed in; an empty entry counts as none
    return m_settings.objectId.isEmpty() || notFound || error.isEmpty() ? StoredToken::Missing : StoredToken::Unknown;
}

void Auth::logout()
{
    QMutexLocker locker(&m_refreshMutex);
    setToken({}, {});
    m_refreshToken.clear();
    if (m_settings.objectId.isEmpty() || !validKeyParts(m_settings.tenant, m_settings.clientId, m_settings.objectId)) {
        // Nothing of a user to remove
        return;
    }
    keychainDelete(m_settings.keychainService, keychainKey());
}

SignedInUser Auth::parseIdToken(const QString &idToken)
{
    const QJsonObject claims = jwtClaims(idToken);
    SignedInUser user{SignIn::normalizedId(claims.value(QLatin1String("oid")).toString()),
                      claims.value(QLatin1String("preferred_username")).toString(),
                      claims.value(QLatin1String("name")).toString(),
                      SignIn::normalizedId(claims.value(QLatin1String("tid")).toString())};
    if (!SignIn::isGuid(user.objectId) || !SignIn::isGuid(user.tenantId)) {
        return {};
    }
    return user;
}

QString Auth::readRefreshToken(QString *error, bool *notFound)
{
    // Every stored sign-in belongs to a user
    if (m_settings.objectId.isEmpty() || !validKeyParts(m_settings.tenant, m_settings.clientId, m_settings.objectId)) {
        if (notFound) {
            *notFound = true;
        }
        return {};
    }
    QString token;
    QKeychain::Error code = QKeychain::NoError;
    if (!keychainRead(m_settings.keychainService, keychainKey(), &token, error, &code)) {
        if (notFound) {
            *notFound = code == QKeychain::EntryNotFound;
        }
        return {};
    }
    return token;
}

bool Auth::writeRefreshTokenFor(const AuthSettings &settings, const QString &token, QString *error)
{
    // Only under a user's own key, with parts that cannot hold the key's separator
    if (settings.objectId.isEmpty() || !validKeyParts(settings.tenant, settings.clientId, settings.objectId)) {
        *error = QStringLiteral("no signed-in user to store the sign-in for");
        return false;
    }
    return keychainWrite(settings.keychainService, keychainKeyFor(settings.tenant, settings.clientId, settings.objectId), token, error);
}

Auth::CopyResult Auth::copyToken(const QString &fromService, const QString &fromKey, const QString &toService, const QString &toKey, bool removeSource, QString *error)
{
    QString token;
    QKeychain::Error code = QKeychain::NoError;
    if (!keychainRead(fromService, fromKey, &token, error, &code)) {
        return code == QKeychain::EntryNotFound ? CopyResult::NotFound : CopyResult::Failed;
    }
    if (token.isEmpty()) {
        return CopyResult::NotFound;
    }
    if (!keychainWrite(toService, toKey, token, error)) {
        return CopyResult::Failed;
    }
    if (removeSource && (fromService != toService || fromKey != toKey)) {
        keychainDelete(fromService, fromKey);
    }
    return CopyResult::Copied;
}

}
