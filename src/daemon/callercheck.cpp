// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "callercheck.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace GraphCore::CallerCheck
{

QStringList programNames()
{
    return {
        // This project
        QStringLiteral("graphaccountsctl"),
        // Graph Mail
        QStringLiteral("graphmaild"),
        QStringLiteral("graphmailctl"),
        QStringLiteral("plasma-graphmail-linux"),
        // Graph Sync
        QStringLiteral("graphsyncd"),
        QStringLiteral("graphsyncctl"),
        QStringLiteral("plasma-graphsync-linux"),
        // Graph Chat (its call window needs no token)
        QStringLiteral("graphchatd"),
        QStringLiteral("graphchatctl"),
        QStringLiteral("plasma-graphchat-linux"),
    };
}

QStringList managerNames()
{
    return {QStringLiteral("systemsettings"), QStringLiteral("kcmshell6")};
}

Allowed allowedFor(const QStringList &directories, const QStringList &extra, const QStringList &names)
{
    Allowed allowed;
    for (const QString &directory : directories) {
        if (directory.isEmpty()) {
            continue;
        }
        const QString clean = QDir::cleanPath(directory);
        for (const QString &name : names) {
            allowed.files.append(clean + QLatin1Char('/') + name);
        }
    }
    for (const QString &entry : extra) {
        const QString trimmed = entry.trimmed();
        // Only absolute paths: a relative one would depend on where the service was started
        if (!trimmed.startsWith(QLatin1Char('/'))) {
            continue;
        }
        if (trimmed.endsWith(QLatin1Char('/'))) {
            allowed.directories.append(QDir::cleanPath(trimmed));
        } else {
            allowed.files.append(QDir::cleanPath(trimmed));
        }
    }
    allowed.files.removeDuplicates();
    allowed.directories.removeDuplicates();
    return allowed;
}

QString executableOf(quint32 pid)
{
    if (pid == 0) {
        return {};
    }
    QString target = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    const QLatin1StringView deleted(" (deleted)");
    if (target.endsWith(deleted)) {
        target.chop(deleted.size());
    }
    return target;
}

bool isAllowed(const QString &executable, const Allowed &allowed)
{
    if (executable.isEmpty() || !executable.startsWith(QLatin1Char('/'))) {
        return false;
    }
    const QString clean = QDir::cleanPath(executable);
    if (allowed.files.contains(clean)) {
        return true;
    }
    return allowed.directories.contains(QFileInfo(clean).path());
}

}
