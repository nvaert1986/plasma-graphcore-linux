// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Graph errors and the account service's problems as messages for people (translated, catalog
// plasma-graphcore-linux), the same in every app of the suite. Logs keep the technical text
// (GraphResponse::describe); these say what happened and what to do, with the code in brackets
// for whoever reads it to support.
namespace GraphCore::Errors
{

// An answer from Graph (status 100-599) or none at all (status 0, networkError: Qt's text).
// graphCode: Graph's error code ("accessDenied", "ErrorItemNotFound", ...), may be empty
QString message(int status, const QString &graphCode, const QString &networkError = {});

// A problem name from the account service (AccountService::problemOf / ServiceTokenProvider::
// lastProblem: NotSignedIn, SignInExpired, ConsentRequired, Offline, KeychainLocked, NotAllowed,
// UnknownAccount, ...); a generic text for an unknown one, empty for an empty one
QString problemMessage(const QString &problem);

}
