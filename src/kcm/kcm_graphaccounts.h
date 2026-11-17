// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>

#include <QVariantList>

namespace GraphCore
{
class AccountsClient;
}

// "Microsoft 365 Accounts" in System Settings: the accounts of the suite (one sign-in for every app),
// add one, sign in again, sign out. Everything goes through the account service, which lets System
// Settings manage accounts but never gives it a token. Changes apply at once: no Apply button.
class KcmGraphAccounts : public KQuickConfigModule
{
    Q_OBJECT
    // [{id, displayName, userPrincipalName, organization, state, stateText}]
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    // The service answers (it starts on demand); false: not installed or broken
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY accountsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY signInChanged)
    // A sign-in with a code: the code and the page to enter it on
    Q_PROPERTY(QString deviceCode READ deviceCode NOTIFY signInChanged)
    Q_PROPERTY(QString deviceCodeAddress READ deviceCodeAddress NOTIFY signInChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY signInChanged)
    // When the organisation has not approved the apps: the page its administrator opens once
    Q_PROPERTY(QString adminConsentLink READ adminConsentLink NOTIFY signInChanged)
    // The account of the last sign-in that worked (its address)
    Q_PROPERTY(QString signedInAs READ signedInAs NOTIFY signInChanged)
    // Builds without the browser sign-in (Qt before 6.9) sign in with a code only
    Q_PROPERTY(bool browserSignIn READ browserSignIn CONSTANT)

public:
    KcmGraphAccounts(QObject *parent, const KPluginMetaData &data);
    ~KcmGraphAccounts() override;

    QVariantList accounts() const;
    bool serviceAvailable() const;
    bool busy() const;
    QString deviceCode() const;
    QString deviceCodeAddress() const;
    QString lastError() const;
    QString adminConsentLink() const;
    QString signedInAs() const;
    bool browserSignIn() const;

    // A new account: the work address (organisation and login hint), optionally the organisation's
    // own registration (client ID or IT's setup text)
    Q_INVOKABLE bool addAccount(const QString &email, const QString &registration, bool withCode);
    // The same account again (its registration, organisation and address)
    Q_INVOKABLE bool signInAgain(const QString &accountId, bool withCode);
    Q_INVOKABLE void cancelSignIn();
    // Signs the account out for every app of the suite
    Q_INVOKABLE bool signOut(const QString &accountId);
    Q_INVOKABLE void copyText(const QString &text);
    // Forgets the last result (a new dialog starts clean)
    Q_INVOKABLE void clearResult();

Q_SIGNALS:
    void accountsChanged();
    void signInChanged();
    // A sign-in ended: ok, or lastError says why
    void signInDone(bool ok);

private:
    void reload();
    bool start(const QString &registration, const QString &tenant, const QString &hint, bool withCode);
    void finished(const QString &request, const QString &account, const QString &error);

    GraphCore::AccountsClient *m_client;
    QVariantList m_accounts;
    bool m_available = false;
    QString m_request;
    // For the approval link of a sign-in that needs it
    QString m_requestTenant;
    QString m_requestClient;
    QString m_deviceCode;
    QString m_deviceCodeAddress;
    QString m_lastError;
    QString m_adminConsentLink;
    QString m_signedInAs;
};
