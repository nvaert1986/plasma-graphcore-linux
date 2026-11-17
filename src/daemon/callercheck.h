// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// Which programs may ask the account service for anything (docs/DESIGN.md section 3). Hygiene, not
// a security boundary: code running as the same user could also read the open wallet. It keeps any
// other program on the session bus from simply asking for a token.
namespace GraphCore::CallerCheck
{

// The suite's programs, by file name
QStringList programNames();

// The executables allowed: every program name in each directory, plus extra entries (from
// graphaccountsrc [Development] AllowedExecutables): a path ending in "/" allows every file
// directly in that directory (a build tree), anything else is one exact file
struct Allowed {
    QStringList files;
    QStringList directories;
};
Allowed allowedFor(const QStringList &directories, const QStringList &extra);

// The executable of a running process (/proc/<pid>/exe); a binary replaced by a reinstall since it
// started (" (deleted)") still counts as its path. Empty when it cannot be read
QString executableOf(quint32 pid);

bool isAllowed(const QString &executable, const Allowed &allowed);

}
