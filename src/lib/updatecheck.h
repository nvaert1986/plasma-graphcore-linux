// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;

// New versions of the suite's apps: packages from GitHub come from no repository, so no package
// manager tells their users about a new release. Once a day the app asks GitHub for its newest
// release; nothing but the request itself is sent. What to show (a notification, a settings page)
// stays with the app.
namespace GraphCore
{

namespace UpdateCheck
{
struct Release {
    QString version;
    // The release page with the packages and the notes
    QUrl page;
};

// The release in GitHub's answer to releases/latest of repository ("owner/name"); nullopt for a
// draft, a pre-release or an answer that is not understood. The page is made here, never taken
// from the answer
std::optional<Release> parseLatest(const QByteArray &json, const QString &repository);
// A plain version (1.2.3, also with a leading v) or empty when it is none
QString cleanVersion(const QString &tag);
// candidate is a later version than current
bool isNewer(const QString &candidate, const QString &current);
// The release page of a version of repository
QUrl releasePage(const QString &repository, const QString &version);
}

struct UpdateCheckSettings {
    // GitHub repository, "owner/name"
    QString repository;
    // The version running here
    QString currentVersion;
    // Where the last answer, the announced and the skipped version are kept (a small config file
    // in the app's state folder), so a restart does not ask again
    QString stateFile;
    // GitHub refuses requests without one: "<app>/<version>"
    QString userAgent;
    // Empty: GitHub's releases/latest of repository. Tests point it at a local server
    QUrl latestUrl;
};

// Asks GitHub and keeps what it said in the state file
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(const UpdateCheckSettings &settings, QObject *parent = nullptr);

    UpdateCheckSettings settings() const;

    // Asks GitHub when the last answer is a day old (force: now); checked() follows. Without asking,
    // it only looks at the last answer again.
    void check(bool force = false);
    // The newer version from the last answer, unless skipped; empty when there is none
    QString available() const;
    QUrl page() const;
    // The newest release in GitHub's last answer, also when it is not newer than this version
    QString latest() const;
    // When GitHub last answered (invalid: never)
    QDateTime lastCheck() const;
    // Waiting for GitHub
    bool busy() const
    {
        return m_busy;
    }
    // A newer version is told about at every login (afterLogin) and once a day, until skipped:
    // announce() says whether it is time, markAnnounced() remembers when it was told
    bool announce(bool afterLogin) const;
    void markAnnounced();
    // "Skip This Version": not told about again
    void skip(const QString &version);

Q_SIGNALS:
    // ok false: GitHub could not be asked (error says why); the next check tries again
    void checked(bool ok, const QString &error);

private:
    UpdateCheckSettings m_settings;
    QNetworkAccessManager *m_network = nullptr;
    bool m_busy = false;
};

}
