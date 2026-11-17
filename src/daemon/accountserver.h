// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "callercheck.h"

#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>

class KNotification;

namespace GraphCore
{

struct SignedInUser;

// The account service (docs/DESIGN.md sections 3 and 4): the one holder of the suite's refresh
// tokens. Hands out access tokens over D-Bus to the suite's own programs, does the sign-in, takes
// over the sign-ins the apps stored before it existed.
class AccountServer : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.nvaert1986.GraphAccounts1")

public:
    // The keychain service the refresh tokens are stored under
    static QString keychainService();
    // The apps whose stored sign-ins are taken over (their keychain services)
    static QStringList earlierApps();

    explicit AccountServer(QObject *parent = nullptr);
    ~AccountServer() override;

    bool registerOnBus(QString *error);

public Q_SLOTS:
    // JSON list: id, tenantId, objectId, userPrincipalName, displayName, organization, state
    Q_SCRIPTABLE QString Accounts();
    // An access token for a permission set ("suite", "directory") and when it expires (ms since
    // the epoch, UTC). forceRefresh after a 401 from Graph. Errors: see GraphCore::AccountService::errorName
    Q_SCRIPTABLE QString AccessToken(const QString &account, const QString &scopeSet, bool forceRefresh, qlonglong &expiresAtMs);
    // Starts a sign-in; returns its request id. SignInProgress (device code) and SignInFinished follow
    Q_SCRIPTABLE QString SignIn(const QString &clientIdOrSetupText, const QString &tenantOrAddress, const QString &loginHint, bool deviceCode);
    Q_SCRIPTABLE void CancelSignIn(const QString &request);
    // Signs the account out for every app of the suite (also the apps' own stored sign-ins)
    Q_SCRIPTABLE void SignOut(const QString &account);

Q_SIGNALS:
    Q_SCRIPTABLE void AccountsChanged();
    Q_SCRIPTABLE void SignInNeeded(const QString &account, const QString &reason);
    Q_SCRIPTABLE void SignInProgress(const QString &request, const QString &code, const QString &address);
    Q_SCRIPTABLE void SignInFinished(const QString &request, const QString &account, const QString &error);

private:
    struct State;
    bool callerAllowed();
    // The account's state, made (again) when the account is new or changed in graphaccountsrc
    std::shared_ptr<State> stateFor(const QString &id);
    void setStatus(const std::shared_ptr<State> &state, const QString &status, const QString &problem);
    // The apps' own stored sign-ins of an account, once the service's own works: removed
    void removeEarlierSignIns(const std::shared_ptr<State> &state);
    void finishSignIn(const QString &request, const QString &clientId, const SignedInUser &user, const QString &organization, bool ok, const QString &error);
    void refuse(const QString &problem, const QString &text);
    // A sign-in on its own thread (no caller check: SignIn and the notification's action); the request id
    QString startSignIn(const QString &client, const QString &tenant, const QString &loginHint, bool deviceCode);
    // One "Sign in again" notification per account for the whole suite (none without a display)
    void notifySignInNeeded(const std::shared_ptr<State> &state, const QString &problem);

    CallerCheck::Allowed m_allowed;
    QHash<QString, bool> m_callers;
    QHash<QString, std::shared_ptr<State>> m_states;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_signIns;
    // The open "Sign in again" notifications by account; closed once the account works again
    QHash<QString, QPointer<KNotification>> m_notifications;
};

}
