// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "accounts.h"
#include "accountserver.h"
#include "auth.h"
#include "graphaccountsd_debug.h"

#include <QCoreApplication>
#include <QUrl>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("graphaccountsd"));
    app.setApplicationVersion(QStringLiteral(GRAPHCORE_VERSION));
    // Sign-ins made here keep their registration under this name in graphaccountsrc
    GraphCore::Accounts::setApplication(GraphCore::AccountServer::keychainService());

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
    qCInfo(GRAPHACCOUNTS) << "Account service" << app.applicationVersion() << "ready";
    return app.exec();
}
