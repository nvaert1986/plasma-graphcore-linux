// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signin.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

namespace GraphCore::SignIn
{

bool isGuid(const QString &text)
{
    static const QRegularExpression guid(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return guid.match(text).hasMatch();
}

QString normalizedId(const QString &id)
{
    return id.trimmed().toLower();
}

QString resolveTenant(const QString &input, bool *offline)
{
    *offline = false;
    QString value = input.trimmed();
    if (isGuid(value)) {
        return value.toLower();
    }
    // A work e-mail address stands for its domain
    value = value.section(QLatin1Char('@'), -1).toLower();
    static const QRegularExpression domain(QStringLiteral("^[a-z0-9-]+(\\.[a-z0-9-]+)+$"));
    if (!domain.match(value).hasMatch()) {
        return {};
    }

    QNetworkAccessManager nam;
    QNetworkRequest request(QUrl(QStringLiteral("https://login.microsoftonline.com/%1/v2.0/.well-known/openid-configuration").arg(value)));
    request.setTransferTimeout(30 * 1000);
    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) {
        *offline = true;
        return {};
    }
    // "issuer": "https://login.microsoftonline.com/<tenant id>/v2.0"
    const QString issuer = QJsonDocument::fromJson(reply->readAll()).object().value(QLatin1String("issuer")).toString();
    const QString tenantId = QUrl(issuer).path().section(QLatin1Char('/'), 1, 1);
    return isGuid(tenantId) ? tenantId.toLower() : QString();
}

SetupText parseSetupText(const QString &text)
{
    static const QRegularExpression client(QStringLiteral("ClientId\\s*[=:]\\s*([0-9a-fA-F-]{36})"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tenant(QStringLiteral("TenantId\\s*[=:]\\s*([^\\s;,]+)"), QRegularExpression::CaseInsensitiveOption);
    return {client.match(text).captured(1), tenant.match(text).captured(1)};
}

QUrl adminConsentUrl(const QString &tenant, const QString &clientId)
{
    QUrl url(QStringLiteral("https://login.microsoftonline.com/%1/adminconsent").arg(tenant.isEmpty() ? QStringLiteral("organizations") : tenant));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    url.setQuery(query);
    return url;
}

bool needsAdminConsent(const QString &error)
{
    // AADSTS65001: not consented; 90094 / 90099: admin approval required; 700016: the app does not
    // exist in that directory (never approved there, or the tenant is not on the allowed list);
    // consent_required comes back from the browser sign-in
    static const QRegularExpression codes(QStringLiteral("AADSTS(65001|90094|90099|700016)\\b|consent_required"));
    return codes.match(error).hasMatch();
}

}
