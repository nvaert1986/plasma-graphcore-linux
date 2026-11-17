// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcm_graphaccounts.h"

#include "accounts.h"
#include "errors.h"
#include "serviceclient.h"
#include "signin.h"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QClipboard>
#include <QGuiApplication>
#include <QVariantMap>

#include <algorithm>

K_PLUGIN_CLASS_WITH_JSON(KcmGraphAccounts, "kcm_graphaccounts.json")

namespace
{
// The keychain service of the account service: an account's registration is kept under its key
const QString serviceApp = QStringLiteral("plasma-graphcore-linux");

QString stateText(const QString &state)
{
    if (state == QLatin1String("signedIn")) {
        return i18nc("@info:status account", "Signed in");
    }
    if (state == QLatin1String("signInNeeded")) {
        return i18nc("@info:status account", "Sign-in needed");
    }
    if (state == QLatin1String("keychainLocked")) {
        return i18nc("@info:status account", "Wallet locked");
    }
    if (state == QLatin1String("signedOut")) {
        return i18nc("@info:status account", "Signed out");
    }
    // Not asked since the service started: no app has used it yet
    return {};
}

// The organisation part of a work address (the service also takes the whole address)
QString domainOf(const QString &email)
{
    const int at = email.lastIndexOf(QLatin1Char('@'));
    return at < 0 ? email.trimmed() : email.mid(at + 1).trimmed();
}
}

KcmGraphAccounts::KcmGraphAccounts(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
    , m_client(new GraphCore::AccountsClient(this))
{
    setButtons(NoAdditionalButton);
    connect(m_client, &GraphCore::AccountsClient::accountsChanged, this, &KcmGraphAccounts::reload);
    connect(m_client, &GraphCore::AccountsClient::signInNeeded, this, &KcmGraphAccounts::reload);
    connect(m_client, &GraphCore::AccountsClient::signInProgress, this, [this](const QString &request, const QString &code, const QString &address) {
        if (request != m_request) {
            return;
        }
        m_deviceCode = code;
        m_deviceCodeAddress = address;
        Q_EMIT signInChanged();
    });
    connect(m_client, &GraphCore::AccountsClient::signInFinished, this, &KcmGraphAccounts::finished);
    reload();
}

KcmGraphAccounts::~KcmGraphAccounts()
{
    // A browser or code sign-in still waiting is not left behind in the service
    if (!m_request.isEmpty()) {
        m_client->cancelSignIn(m_request);
    }
}

void KcmGraphAccounts::reload()
{
    QString error;
    QList<GraphCore::ServiceAccount> all = m_client->accounts(&error);
    // By name, the same order every time
    std::sort(all.begin(), all.end(), [](const GraphCore::ServiceAccount &a, const GraphCore::ServiceAccount &b) {
        const QString left = a.displayName.isEmpty() ? a.userPrincipalName : a.displayName;
        const QString right = b.displayName.isEmpty() ? b.userPrincipalName : b.displayName;
        return QString::localeAwareCompare(left, right) < 0;
    });
    m_available = error.isEmpty();
    m_accounts.clear();
    for (const GraphCore::ServiceAccount &a : all) {
        m_accounts.append(QVariantMap{{QStringLiteral("id"), a.id},
                                      {QStringLiteral("displayName"), a.displayName},
                                      {QStringLiteral("userPrincipalName"), a.userPrincipalName},
                                      {QStringLiteral("organization"), a.organization},
                                      {QStringLiteral("state"), a.state},
                                      {QStringLiteral("stateText"), stateText(a.state)}});
    }
    Q_EMIT accountsChanged();
}

QVariantList KcmGraphAccounts::accounts() const
{
    return m_accounts;
}

bool KcmGraphAccounts::serviceAvailable() const
{
    return m_available;
}

bool KcmGraphAccounts::busy() const
{
    return !m_request.isEmpty();
}

QString KcmGraphAccounts::deviceCode() const
{
    return m_deviceCode;
}

QString KcmGraphAccounts::deviceCodeAddress() const
{
    return m_deviceCodeAddress;
}

QString KcmGraphAccounts::lastError() const
{
    return m_lastError;
}

QString KcmGraphAccounts::adminConsentLink() const
{
    return m_adminConsentLink;
}

QString KcmGraphAccounts::signedInAs() const
{
    return m_signedInAs;
}

bool KcmGraphAccounts::browserSignIn() const
{
    return GraphCore::SignIn::browserLoginAvailable();
}

bool KcmGraphAccounts::addAccount(const QString &email, const QString &registration, bool withCode)
{
    const QString address = email.trimmed();
    if (!address.contains(QLatin1Char('@'))) {
        m_lastError = i18nc("@info", "Enter your work e-mail address.");
        Q_EMIT signInChanged();
        return false;
    }
    return start(registration.trimmed(), domainOf(address), address, withCode);
}

bool KcmGraphAccounts::signInAgain(const QString &accountId, bool withCode)
{
    const GraphCore::Account account = GraphCore::Accounts::find(accountId);
    if (!account.isValid()) {
        m_lastError = GraphCore::Errors::problemMessage(QStringLiteral("UnknownAccount"));
        Q_EMIT signInChanged();
        return false;
    }
    // The registration the service signed this account in with
    return start(GraphCore::Accounts::clientIdOf(accountId, serviceApp), account.tenantId, account.userPrincipalName, withCode);
}

bool KcmGraphAccounts::start(const QString &registration, const QString &tenant, const QString &hint, bool withCode)
{
    if (!m_request.isEmpty()) {
        return false;
    }
    clearResult();
    QString error;
    const QString request = m_client->signIn(registration, tenant, hint, withCode || !browserSignIn(), &error);
    if (request.isEmpty()) {
        m_lastError = error.isEmpty() ? GraphCore::Errors::problemMessage(QStringLiteral("Other")) : error;
        Q_EMIT signInChanged();
        return false;
    }
    m_request = request;
    m_requestTenant = tenant;
    const GraphCore::SignIn::SetupText setup = GraphCore::SignIn::parseSetupText(registration);
    m_requestClient = !setup.clientId.isEmpty() ? setup.clientId : (registration.isEmpty() ? GraphCore::Accounts::defaultClientId() : registration);
    Q_EMIT signInChanged();
    return true;
}

void KcmGraphAccounts::finished(const QString &request, const QString &account, const QString &error)
{
    if (request != m_request) {
        return;
    }
    m_request.clear();
    m_deviceCode.clear();
    m_deviceCodeAddress.clear();
    const bool ok = error.isEmpty() && !account.isEmpty();
    if (ok) {
        m_signedInAs = GraphCore::Accounts::find(account).userPrincipalName;
    } else if (GraphCore::SignIn::needsAdminConsent(error)) {
        m_lastError = i18nc("@info", "Your organisation has not approved the Microsoft 365 apps of this suite yet. An administrator of the organisation opens the approval page once, signs in and accepts; then sign in again here.");
        m_adminConsentLink = GraphCore::SignIn::adminConsentUrl(m_requestTenant, m_requestClient).toString();
    } else if (error.contains(QLatin1String("cancel"), Qt::CaseInsensitive)) {
        m_lastError.clear();
    } else if (error.startsWith(QLatin1String("no organisation found"))) {
        m_lastError = i18nc("@info", "No Microsoft 365 organisation uses this address. Check the address.");
    } else if (error.startsWith(QLatin1String("no connection"))) {
        m_lastError = GraphCore::Errors::problemMessage(QStringLiteral("Offline"));
    } else {
        m_lastError = i18nc("@info %1 the technical reason", "The sign-in did not complete. (%1)", error);
    }
    reload();
    Q_EMIT signInChanged();
    Q_EMIT signInDone(ok);
}

void KcmGraphAccounts::cancelSignIn()
{
    if (!m_request.isEmpty()) {
        m_client->cancelSignIn(m_request);
    }
}

bool KcmGraphAccounts::signOut(const QString &accountId)
{
    QString error;
    const bool ok = m_client->signOut(accountId, &error);
    if (!ok) {
        m_lastError = error;
        Q_EMIT signInChanged();
    }
    reload();
    return ok;
}

void KcmGraphAccounts::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void KcmGraphAccounts::clearResult()
{
    m_lastError.clear();
    m_adminConsentLink.clear();
    m_signedInAs.clear();
    Q_EMIT signInChanged();
}

#include "kcm_graphaccounts.moc"
