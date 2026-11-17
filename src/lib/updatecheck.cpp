// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "updatecheck.h"
#include "graphcore_debug.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>

namespace GraphCore
{

namespace
{
constexpr qint64 checkEverySecs = 24 * 3600;
// A little under a day: apps run the daily check on an hourly timer
constexpr qint64 remindEverySecs = 23 * 3600;
constexpr int timeoutMs = 30 * 1000;

KConfigGroup state(KConfig &config)
{
    return config.group(QStringLiteral("Update"));
}
}

namespace UpdateCheck
{
QString cleanVersion(const QString &tag)
{
    static const QRegularExpression plain(QStringLiteral("^v?(\\d{1,4}(?:\\.\\d{1,4}){1,3})$"));
    const QRegularExpressionMatch match = plain.match(tag.trimmed());
    return match.hasMatch() ? match.captured(1) : QString();
}

QUrl releasePage(const QString &repository, const QString &version)
{
    return QUrl(QStringLiteral("https://github.com/%1/releases/tag/v%2").arg(repository, version));
}

std::optional<Release> parseLatest(const QByteArray &json, const QString &repository)
{
    const QJsonObject release = QJsonDocument::fromJson(json).object();
    if (release.value(QLatin1String("draft")).toBool() || release.value(QLatin1String("prerelease")).toBool()) {
        return std::nullopt;
    }
    // Only a plain version; the page is made here, not taken from the answer
    const QString version = cleanVersion(release.value(QLatin1String("tag_name")).toString());
    if (version.isEmpty()) {
        return std::nullopt;
    }
    return Release{version, releasePage(repository, version)};
}

bool isNewer(const QString &candidate, const QString &current)
{
    const QString a = cleanVersion(candidate);
    const QString b = cleanVersion(current);
    return !a.isEmpty() && !b.isEmpty() && QVersionNumber::compare(QVersionNumber::fromString(a), QVersionNumber::fromString(b)) > 0;
}
}

UpdateChecker::UpdateChecker(const UpdateCheckSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
{
}

UpdateCheckSettings UpdateChecker::settings() const
{
    return m_settings;
}

void UpdateChecker::check(bool force)
{
    if (m_busy) {
        return;
    }
    {
        KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
        const QDateTime last = QDateTime::fromString(state(config).readEntry("LastCheck", QString()), Qt::ISODate);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (!force && last.isValid() && last <= now && last.secsTo(now) < checkEverySecs) {
            Q_EMIT checked(true, {});
            return;
        }
    }
    m_busy = true;
    const QUrl url = m_settings.latestUrl.isEmpty() ? QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(m_settings.repository))
                                                    : m_settings.latestUrl;
    QNetworkRequest request(url);
    // GitHub refuses requests without a User-Agent
    request.setHeader(QNetworkRequest::UserAgentHeader, m_settings.userAgent);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(timeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_busy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            const QString error = reply->error() != QNetworkReply::NoError ? reply->errorString() : QStringLiteral("HTTP %1").arg(status);
            qCInfo(GRAPHCORE) << "Could not look for a new version:" << error;
            Q_EMIT checked(false, error);
            return;
        }
        const std::optional<UpdateCheck::Release> release = UpdateCheck::parseLatest(reply->readAll(), m_settings.repository);
        if (!release) {
            qCInfo(GRAPHCORE) << "Could not look for a new version: unexpected answer";
            Q_EMIT checked(false, QStringLiteral("unexpected answer"));
            return;
        }
        QDir().mkpath(QFileInfo(m_settings.stateFile).path());
        KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
        KConfigGroup group = state(config);
        group.writeEntry("LastCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        group.writeEntry("Latest", release->version);
        config.sync();
        qCInfo(GRAPHCORE).noquote() << "Newest release of" << m_settings.repository << release->version;
        Q_EMIT checked(true, {});
    });
}

QString UpdateChecker::available() const
{
    KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
    const KConfigGroup group = state(config);
    const QString latest = UpdateCheck::cleanVersion(group.readEntry("Latest", QString()));
    if (!UpdateCheck::isNewer(latest, m_settings.currentVersion) || latest == group.readEntry("Skipped", QString())) {
        return {};
    }
    return latest;
}

QUrl UpdateChecker::page() const
{
    const QString version = available();
    return version.isEmpty() ? QUrl() : UpdateCheck::releasePage(m_settings.repository, version);
}

QString UpdateChecker::latest() const
{
    KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
    return UpdateCheck::cleanVersion(state(config).readEntry("Latest", QString()));
}

QDateTime UpdateChecker::lastCheck() const
{
    KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
    return QDateTime::fromString(state(config).readEntry("LastCheck", QString()), Qt::ISODate);
}

bool UpdateChecker::announce(bool afterLogin) const
{
    const QString version = available();
    if (version.isEmpty()) {
        return false;
    }
    if (afterLogin) {
        return true;
    }
    KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
    const KConfigGroup group = state(config);
    const QDateTime last = QDateTime::fromString(group.readEntry("AnnouncedAt", QString()), Qt::ISODate);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return group.readEntry("Announced", QString()) != version || !last.isValid() || last > now || last.secsTo(now) >= remindEverySecs;
}

void UpdateChecker::markAnnounced()
{
    const QString version = available();
    KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
    KConfigGroup group = state(config);
    group.writeEntry("Announced", version);
    group.writeEntry("AnnouncedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    config.sync();
}

void UpdateChecker::skip(const QString &version)
{
    KConfig config(m_settings.stateFile, KConfig::SimpleConfig);
    state(config).writeEntry("Skipped", UpdateCheck::cleanVersion(version));
    config.sync();
}

}
