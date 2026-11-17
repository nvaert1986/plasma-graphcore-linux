// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "accounts.h"
#include "tokenprovider.h"

#include <QDateTime>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <functional>

namespace GraphCore
{

struct AuthSettings {
    // Keychain service the refresh token is stored under (the account service's own)
    QString keychainService;
    QString clientId;
    // A tenant ID, or "organizations" for a first sign-in where the user picks any work account
    QString tenant;
    // The user's object id; empty until signed in
    QString objectId;
    QStringList scopes;
};

// Who signed in, from the ID token
struct SignedInUser {
    QString objectId;
    QString userPrincipalName;
    QString displayName;
    QString tenantId;
};

// Entra ID sign-in for one account with a public client (authorization code + PKCE, or a device
// code). The refresh token lives in the system keychain (KWallet / Secret Service) under
// tenant:client:user; access tokens only in memory. Thread-safe: callers ask accessToken() from
// their own threads; cachedAccessToken() never waits for a refresh.
//
// Only the account service (graphaccountsd) uses this: it is the one holder of refresh tokens.
// Merged from Graph Mail's and Graph Sync's own copies (phase 10).
class Auth : public TokenProvider
{
public:
    explicit Auth(const AuthSettings &settings);

    // Settings for an account that signed in before
    static AuthSettings settingsFor(const Account &account, const QString &keychainService, const QStringList &scopes);
    // The account to store after a sign-in (organisation still empty: it comes from Graph)
    static Account accountFor(const SignedInUser &user, const QString &clientId);

    // A registration (client ID) and a tenant to sign in to, parts that fit in a keychain key
    bool isConfigured() const;
    AuthSettings settings() const;

    // Opens the browser and blocks (with a local event loop) until the redirect arrives.
    // Must run in a thread with an event loop. loginHint pre-fills the account picker.
    bool interactiveLogin(QString *error, const QString &loginHint = {});
    // Sign-in with a short code entered at the shown address, on any device. show() gets the code
    // and the address as soon as they are known; then this blocks until the user is done, the
    // code expires or cancel is set. Needs "Allow public client flows" on the app registration.
    bool deviceCodeLogin(const std::function<void(const QString &code, const QString &address)> &show, const std::atomic_bool *cancel, QString *error);
    // After a sign-in: the user, whose tenant and object id the settings now carry
    SignedInUser signedInUser() const;

    // Why a token could not be had. Only SignInExpired and ConsentRequired need the user; Offline
    // and KeychainLocked go away by themselves
    enum class Problem {
        None,
        NotSignedIn,
        SignInExpired,
        ConsentRequired,
        Offline,
        KeychainLocked,
        Other,
    };
    QString accessToken(QString *error, bool forceRefresh, Problem *problem);
    QString accessToken(QString *error, bool forceRefresh = false) override;
    QString cachedAccessToken() override;

    bool hasStoredRefreshToken();
    // What the keychain holds for this account: Unknown when it cannot be read (locked, no
    // keychain service), so the caller does not mistake that for "never signed in"
    enum class StoredToken { Present, Missing, Unknown };
    StoredToken storedRefreshToken();
    void logout();

    // The claims of an ID token we care about (IDs in lower case); empty fields when it cannot be
    // read or names no user
    static SignedInUser parseIdToken(const QString &idToken);

    // The keychain entry name of a sign-in: tenant:client:user
    static QString keychainKeyFor(const QString &tenant, const QString &clientId, const QString &objectId);
    // Copies a refresh token between two keychain entries (the account service taking over the
    // apps' sign-ins); the source stays unless removeSource. NotFound: the source has none;
    // Failed: the keychain could not be read or written (locked), try again later
    enum class CopyResult { Copied, NotFound, Failed };
    static CopyResult copyToken(const QString &fromService, const QString &fromKey, const QString &toService, const QString &toKey, bool removeSource, QString *error);

    // Tests only: token requests go to this loopback server (http://127.0.0.1:<port>) instead of
    // Microsoft; any other address is ignored, so a sign-in is never sent anywhere else
    static void setLoginServerForTesting(const QUrl &server);
    // Tests only: a refresh token in memory, as after a sign-in (no keychain involved)
    void setRefreshTokenForTesting(const QString &token);

private:
    QString authority() const;
    QString scopeString() const;
    QString keychainKey() const;
    bool refresh(QString *error, Problem *problem);
    // The end of every sign-in: stores the refresh token under the new user's key first; nothing
    // else changes unless that worked
    bool completeSignIn(const QString &accessToken, qint64 expiresInSeconds, const QString &refreshToken, const QString &idToken, QString *error);
    void setToken(const QString &token, const QDateTime &expiresAt);

    QString readRefreshToken(QString *error, bool *notFound = nullptr);
    static bool writeRefreshTokenFor(const AuthSettings &settings, const QString &token, QString *error);

    // m_refreshMutex serialises sign-ins and refreshes (network round trips); m_tokenMutex only
    // guards the cached access token, so cachedAccessToken() never waits for a refresh. Recursive:
    // a sign-in runs a local event loop, and code called from it on the same thread (a timer
    // asking isConfigured()) must not deadlock.
    mutable QRecursiveMutex m_refreshMutex;
    mutable QMutex m_tokenMutex;
    AuthSettings m_settings;
    SignedInUser m_user;
    QString m_refreshToken;
    QString m_accessToken;
    QDateTime m_expiresAt;
};

}
