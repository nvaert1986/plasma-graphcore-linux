// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QUrl>
#include <QtGlobal>

// What the apps' sign-in screens need without holding a sign-in themselves: the account service
// (graphaccountsd) does the sign-in and keeps the tokens
namespace GraphCore::SignIn
{

// Signing in through the browser needs Qt 6.9 (scope tokens, the "localhost" callback host, the
// ID token); older systems (Debian 13: Qt 6.8) sign in with a code only
constexpr bool browserLoginAvailable()
{
    return QT_VERSION >= QT_VERSION_CHECK(6, 9, 0);
}

bool isGuid(const QString &text);

// An ID as stored: trimmed and lower case, so one registration or user has one keychain key
QString normalizedId(const QString &id);

// The tenant ID for what the user typed: a tenant ID, a domain or a work e-mail address. Domains
// are looked up in the identity platform's public OpenID document (no sign-in needed). Blocking.
// Empty when not found; *offline tells a missing connection apart
QString resolveTenant(const QString &input, bool *offline);

// Client ID and tenant from a setup text handed out by IT ("ClientId=... TenantId=...", in any
// layout); fields not found stay empty
struct SetupText {
    QString clientId;
    QString tenant;
};
SetupText parseSetupText(const QString &text);

// Microsoft's page where a tenant administrator approves every permission of the app registration
// for the whole organisation, once
QUrl adminConsentUrl(const QString &tenant, const QString &clientId);

// The sign-in or a token request failed because the organisation has not approved the app (or
// the permissions asked for) yet
bool needsAdminConsent(const QString &error);

}
