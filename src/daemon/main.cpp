// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "accounts.h"
#include "accountserver.h"
#include "auth.h"
#include "graphaccountsd_debug.h"
#include "proxy.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QUrl>

#include <KLocalizedString>

#include <cstdlib>
#include <memory>

int main(int argc, char **argv)
{
    // The "Sign in again" notification needs a GUI application; without a display (ssh, tests) the
    // service runs without it
    std::unique_ptr<QCoreApplication> app;
    if (std::getenv("WAYLAND_DISPLAY") || std::getenv("DISPLAY")) {
        QGuiApplication::setQuitOnLastWindowClosed(false);
        app = std::make_unique<QGuiApplication>(argc, argv);
    } else {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication::setApplicationName(QStringLiteral("graphaccountsd"));
    QCoreApplication::setApplicationVersion(QStringLiteral(GRAPHCORE_VERSION));
    KLocalizedString::setApplicationDomain("plasma-graphcore-linux");
    // Sign-ins made here keep their registration under this name in graphaccountsrc
    GraphCore::Accounts::setApplication(GraphCore::AccountServer::keychainService());
    // Sign-ins and token refreshes through the system's proxy (System Settings > Network > Proxy)
    GraphCore::Proxy::useSystemSettings();

#ifdef GRAPHACCOUNTS_TEST_HOOKS
    // Tests only (graphaccountsd_test): a loopback login server in place of Microsoft
    if (const QString server = qEnvironmentVariable("GRAPHACCOUNTS_TEST_LOGIN_SERVER"); !server.isEmpty()) {
        GraphCore::Auth::setLoginServerForTesting(QUrl(server));
    }
#endif

    GraphCore::AccountServer server;
    QString error;
    if (!server.registerOnBus(&error)) {
        qCCritical(GRAPHACCOUNTS).noquote() << error;
        return 1;
    }
    qCInfo(GRAPHACCOUNTS) << "Account service" << QCoreApplication::applicationVersion() << "ready";
    return app->exec();
}
