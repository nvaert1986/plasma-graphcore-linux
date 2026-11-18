// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// graphaccountsctl: the account service from the command line. It never prints a token.

#include "serviceclient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTextStream>

#include <cstdio>

using namespace GraphCore;

namespace
{
QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

int list()
{
    AccountsClient client;
    QString error;
    const QList<ServiceAccount> accounts = client.accounts(&error);
    if (!error.isEmpty()) {
        err() << "graphaccountsctl: " << error << Qt::endl;
        return 1;
    }
    for (const ServiceAccount &a : accounts) {
        out() << a.id << '\t' << a.userPrincipalName << '\t' << a.organization << '\t' << a.state << Qt::endl;
    }
    return 0;
}

int check(const QString &account, const QString &scopeSet)
{
    ServiceTokenProvider tokens(account, scopeSet);
    QString error;
    if (tokens.accessToken(&error).isEmpty()) {
        const QString problem = tokens.lastProblem();
        err() << "graphaccountsctl: no token" << (problem.isEmpty() ? QString() : QStringLiteral(" (") + problem + QLatin1Char(')')) << ": " << error << Qt::endl;
        return 1;
    }
    out() << "ok" << Qt::endl;
    return 0;
}

int login(const QString &client, const QString &tenant, const QString &hint, bool code)
{
    AccountsClient accounts;
    QEventLoop loop;
    QString request;
    int result = 1;
    QObject::connect(&accounts, &AccountsClient::signInProgress, &loop, [&](const QString &id, const QString &userCode, const QString &address) {
        if (id == request) {
            out() << "Go to " << address << " and enter the code " << userCode << Qt::endl;
        }
    });
    QObject::connect(&accounts, &AccountsClient::signInFinished, &loop, [&](const QString &id, const QString &account, const QString &error) {
        if (id != request) {
            return;
        }
        if (account.isEmpty()) {
            err() << "graphaccountsctl: sign-in failed: " << error << Qt::endl;
        } else {
            out() << "Signed in: account " << account << Qt::endl;
            result = 0;
        }
        loop.quit();
    });
    QString error;
    request = accounts.signIn(client, tenant, hint, code, &error);
    if (request.isEmpty()) {
        err() << "graphaccountsctl: " << error << Qt::endl;
        return 1;
    }
    if (!code) {
        out() << "Sign in in the browser window that opened." << Qt::endl;
    }
    loop.exec();
    return result;
}

int logout(const QString &account)
{
    AccountsClient accounts;
    QString error;
    if (!accounts.signOut(account, &error)) {
        err() << "graphaccountsctl: " << error << Qt::endl;
        return 1;
    }
    out() << "Signed out: account " << account << Qt::endl;
    return 0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("graphaccountsctl"));
    app.setApplicationVersion(QStringLiteral(GRAPHCORE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("The suite's account service from the command line"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list | login | logout <account> | check <account>"));
    parser.addOptions({
        {QStringLiteral("code"), QStringLiteral("login: sign in with a code on any device instead of this browser")},
        {QStringLiteral("client"), QStringLiteral("login: the app registration (client ID) or IT's setup text"), QStringLiteral("id")},
        {QStringLiteral("tenant"), QStringLiteral("login: the organisation (tenant ID, domain or work e-mail address)"), QStringLiteral("tenant")},
        {QStringLiteral("hint"), QStringLiteral("login: the work e-mail address to suggest"), QStringLiteral("address")},
        {QStringLiteral("directory"), QStringLiteral("check: the directory permissions (needs an administrator's consent)")},
        {QStringLiteral("chat"), QStringLiteral("check: Graph Chat's Teams permissions (needs an administrator's consent)")},
    });
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    if (command == QLatin1String("list")) {
        return list();
    }
    if (command == QLatin1String("login")) {
        return login(parser.value(QStringLiteral("client")), parser.value(QStringLiteral("tenant")), parser.value(QStringLiteral("hint")), parser.isSet(QStringLiteral("code")));
    }
    if (command == QLatin1String("logout") && args.size() == 2) {
        return logout(args.at(1));
    }
    if (command == QLatin1String("check") && args.size() == 2) {
        const QString scopeSet = parser.isSet(QStringLiteral("directory")) ? AccountService::directory()
            : parser.isSet(QStringLiteral("chat"))                         ? AccountService::chat()
                                                                           : AccountService::suite();
        return check(args.at(1), scopeSet);
    }
    parser.showHelp(2);
}
