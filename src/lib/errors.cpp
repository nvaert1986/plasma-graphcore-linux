// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "errors.h"

#include <KLocalizedString>

namespace GraphCore::Errors
{

namespace
{
bool codeIs(const QString &code, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (code.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

// Without an answer: what Qt said, sorted into what the user can act on
QString networkMessage(const QString &text)
{
    const QString lower = text.toLower();
    if (lower.contains(QLatin1String("proxy"))) {
        return i18nc("@info %1 the technical reason", "The proxy server did not pass the connection on to Microsoft 365. Check the proxy settings. (%1)", text);
    }
    if (lower.contains(QLatin1String("ssl")) || lower.contains(QLatin1String("tls")) || lower.contains(QLatin1String("certificate"))) {
        return i18nc("@info %1 the technical reason", "The secure connection to Microsoft 365 failed. (%1)", text);
    }
    if (lower.contains(QLatin1String("timed out")) || lower.contains(QLatin1String("timeout"))) {
        return i18nc("@info %1 the technical reason", "Microsoft 365 did not answer in time. It is tried again later. (%1)", text);
    }
    if (lower.contains(QLatin1String("cancel"))) {
        return i18nc("@info", "Cancelled.");
    }
    if (text.isEmpty()) {
        return i18nc("@info", "No connection to Microsoft 365. Check the network connection.");
    }
    return i18nc("@info %1 the technical reason", "No connection to Microsoft 365. Check the network connection. (%1)", text);
}

QString withCode(const QString &sentence, int status, const QString &code)
{
    const QString detail = code.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : QStringLiteral("HTTP %1 %2").arg(status).arg(code);
    return i18nc("@info %1 a sentence, %2 the technical code", "%1 (%2)", sentence, detail);
}
}

QString message(int status, const QString &graphCode, const QString &networkError)
{
    if (status == 0) {
        return networkMessage(networkError);
    }
    const QString &code = graphCode;
    QString sentence;
    if (codeIs(code, {"quotaLimitReached", "ErrorQuotaExceeded", "insufficientStorage", "ErrorQuotaExceededOnDelete"}) || status == 507) {
        sentence = i18nc("@info", "There is not enough storage space online (the mailbox or OneDrive is full).");
    } else if (codeIs(code, {"notAllowed", "ErrorAccessDeniedByPolicy", "accessRestricted"})) {
        sentence = i18nc("@info", "Your organisation does not allow this.");
    } else if (codeIs(code, {"resourceLocked", "ErrorItemLocked"}) || status == 423) {
        sentence = i18nc("@info", "The item is locked, for example because it is open somewhere else. Try again later.");
    } else if (codeIs(code, {"nameAlreadyExists", "ErrorFolderExists", "ErrorDuplicate"})) {
        sentence = i18nc("@info", "An item with this name already exists.");
    } else if (codeIs(code, {"ErrorMailboxMoveInProgress", "MailboxNotEnabledForRESTAPI", "MailboxNotSupportedForRESTAPI", "ErrorMailboxNotEnabledForRESTAPI"})) {
        sentence = i18nc("@info", "This mailbox cannot be reached right now (it may be on another server or being moved).");
    } else if (codeIs(code, {"ErrorSendAsDenied"})) {
        sentence = i18nc("@info", "You are not allowed to send as this address.");
    } else if (codeIs(code, {"ErrorMessageSizeExceeded"}) || status == 413) {
        sentence = i18nc("@info", "The file or message is too large.");
    } else {
        switch (status) {
        case 400:
            sentence = i18nc("@info", "Microsoft 365 did not accept the request.");
            break;
        case 401:
            sentence = i18nc("@info", "The sign-in is no longer valid. Sign in again.");
            break;
        case 403:
            sentence = i18nc("@info", "You do not have permission for this.");
            break;
        case 404:
        case 410:
            sentence = i18nc("@info", "The item no longer exists, or you no longer have access to it.");
            break;
        case 409:
        case 412:
            sentence = i18nc("@info", "The item was changed somewhere else at the same time. Try again.");
            break;
        case 429:
            sentence = i18nc("@info", "Microsoft 365 is limiting the number of requests for a while. It is tried again shortly.");
            break;
        case 500:
        case 502:
        case 503:
        case 504:
            sentence = i18nc("@info", "Microsoft 365 has a temporary problem. It is tried again later.");
            break;
        default:
            sentence = status >= 200 && status < 300 ? QString() : i18nc("@info", "Microsoft 365 answered with an error.");
        }
    }
    return sentence.isEmpty() ? QString() : withCode(sentence, status, code);
}

QString problemMessage(const QString &problem)
{
    if (problem.isEmpty()) {
        return {};
    }
    if (problem == QLatin1String("NotSignedIn")) {
        return i18nc("@info", "This account is not signed in. Sign in under System Settings > Microsoft 365 Accounts.");
    }
    if (problem == QLatin1String("SignInExpired")) {
        return i18nc("@info", "The sign-in of this account expired. Sign in again.");
    }
    if (problem == QLatin1String("ConsentRequired")) {
        return i18nc("@info", "Your organisation's administrator has to approve this app first.");
    }
    if (problem == QLatin1String("Offline")) {
        return i18nc("@info", "No connection to Microsoft 365. Check the network connection.");
    }
    if (problem == QLatin1String("KeychainLocked")) {
        return i18nc("@info", "The wallet that holds the sign-in is locked. Unlock it to continue.");
    }
    if (problem == QLatin1String("NotAllowed")) {
        return i18nc("@info", "This program is not allowed to use the suite's accounts.");
    }
    if (problem == QLatin1String("UnknownAccount")) {
        return i18nc("@info", "This account is no longer on this computer. Add it again under System Settings > Microsoft 365 Accounts.");
    }
    return i18nc("@info %1 the technical reason", "The account service could not get a sign-in. (%1)", problem);
}

}
