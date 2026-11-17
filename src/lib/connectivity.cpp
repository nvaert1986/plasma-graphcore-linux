// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "connectivity.h"

#include <QNetworkInformation>

namespace GraphCore::Connectivity
{

namespace
{
QNetworkInformation *information()
{
    if (!QNetworkInformation::instance()) {
        QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
    }
    return QNetworkInformation::instance();
}
}

bool systemReportsOnline()
{
    QNetworkInformation *info = information();
    if (!info) {
        return true;
    }
    switch (info->reachability()) {
    case QNetworkInformation::Reachability::Disconnected:
    case QNetworkInformation::Reachability::Local:
        return false;
    default:
        return true;
    }
}

bool isMetered()
{
    QNetworkInformation *info = information();
    return info && info->supports(QNetworkInformation::Feature::Metered) && info->isMetered();
}

Watcher::Watcher(QObject *parent)
    : QObject(parent)
    , m_online(systemReportsOnline())
{
    QNetworkInformation *info = information();
    if (!info) {
        return;
    }
    connect(info, &QNetworkInformation::reachabilityChanged, this, [this] {
        const bool online = systemReportsOnline();
        if (online != m_online) {
            m_online = online;
            Q_EMIT onlineChanged(online);
        }
    });
    if (info->supports(QNetworkInformation::Feature::Metered)) {
        connect(info, &QNetworkInformation::isMeteredChanged, this, &Watcher::meteredChanged);
    }
}

bool Watcher::isOnline() const
{
    return m_online;
}

bool Watcher::isMetered() const
{
    return Connectivity::isMetered();
}

}
