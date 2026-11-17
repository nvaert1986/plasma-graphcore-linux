// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QNetworkProxy>
#include <QNetworkProxyQuery>
#include <QString>

// The system's proxy for every Graph and sign-in request: KDE's proxy settings (System Settings >
// Network > Proxy, kioslaverc) as the process's application proxy, so every QNetworkAccessManager
// of the process uses it (also an app's own Graph client). Qt alone only knows the environment
// variables. Settings changed later apply to the next request.
namespace GraphCore::Proxy
{

// Installs the proxy factory for this process; call once at start, before the first request
void useSystemSettings();

// The proxies for a request under the current settings (for tests; the factory uses this)
QList<QNetworkProxy> proxiesFor(const QNetworkProxyQuery &query);

}
