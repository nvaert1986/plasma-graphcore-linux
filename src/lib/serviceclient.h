// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tokenprovider.h"

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

// The apps' side of the account service (graphaccountsd): it owns every sign-in and hands out
// access tokens over D-Bus (docs/DESIGN.md section 3)
namespace GraphCore
{

namespace AccountService
{
QString serviceName();
QString objectPath();
QString interfaceName();
// "<interface>.Error.<problem>"; problems: NotSignedIn, SignInExpired, ConsentRequired, Offline,
// KeychainLocked, NotAllowed, UnknownAccount, InvalidArgs, Other
QString errorName(const QString &problem);
// The problem part of a D-Bus error name from the service (empty for any other error)
QString problemOf(const QString &errorName);
// Names of the permission sets an app may ask a token for
inline QString suite()
{
    return QStringLiteral("suite");
}
inline QString directory()
{
    return QStringLiteral("directory");
}
}

// An account as the service lists it
struct ServiceAccount {
    QString id;
    QString tenantId;
    QString objectId;
    QString userPrincipalName;
    QString displayName;
    QString organization;
    // signedIn, signInNeeded, keychainLocked or unknown (not asked yet)
    QString state;
};

// Access tokens of one account from the service. Thread-safe (Graph requests ask from their own
// threads); a token is kept until five minutes before it expires
class ServiceTokenProvider : public TokenProvider
{
public:
    explicit ServiceTokenProvider(const QString &accountId, const QString &scopeSet = AccountService::suite());

    QString accessToken(QString *error, bool forceRefresh = false) override;
    QString cachedAccessToken() override;

    QString accountId() const;
    // The service's reason for the last failure (a problem name, see AccountService::errorName);
    // empty after a success or for errors of D-Bus itself (no service)
    QString lastProblem() const;

private:
    const QString m_account;
    const QString m_scopeSet;
    mutable QMutex m_mutex;
    QString m_token;
    QDateTime m_expiresAt;
    QString m_lastProblem;
};

// Accounts, sign-in and sign-out through the service, and its signals
class AccountsClient : public QObject
{
    Q_OBJECT

public:
    explicit AccountsClient(QObject *parent = nullptr);

    // Blocking calls (short: the service answers from memory)
    QList<ServiceAccount> accounts(QString *error = nullptr) const;
    // Starts a sign-in in the service (browser, or a device code shown through signInProgress);
    // returns the request id, empty on failure. clientIdOrSetupText may be empty (the suite's
    // registration), a client ID or IT's setup text; tenantOrAddress empty, a tenant, a domain
    // or a work e-mail address
    QString signIn(const QString &clientIdOrSetupText, const QString &tenantOrAddress, const QString &loginHint, bool deviceCode, QString *error = nullptr);
    bool cancelSignIn(const QString &request);
    // Signs the account out for every app of the suite
    bool signOut(const QString &accountId, QString *error = nullptr);

Q_SIGNALS:
    void accountsChanged();
    // The account's sign-in expired or needs an administrator's consent: every app may show it
    void signInNeeded(const QString &account, const QString &reason);
    // A device code sign-in: the code to enter at the address
    void signInProgress(const QString &request, const QString &code, const QString &address);
    // The end of a sign-in: the account, or an error
    void signInFinished(const QString &request, const QString &account, const QString &error);

private Q_SLOTS:
    void onAccountsChanged();
    void onSignInNeeded(const QString &account, const QString &reason);
    void onSignInProgress(const QString &request, const QString &code, const QString &address);
    void onSignInFinished(const QString &request, const QString &account, const QString &error);
};

}
