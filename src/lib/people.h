// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "graphresponse.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Finding people, the same in every app of the suite: the people someone works with (People.Read),
// the organisation's directory (User.ReadBasic.All) and their photos. The requests and the
// answers only, so any Graph client can send them (Graph Mail's asynchronous one, Graph Sync's own).
namespace GraphCore::People
{

struct Person {
    // The directory id (empty for someone outside the organisation)
    QString id;
    QString name;
    QString email;
    QString jobTitle;
    QString department;
};

// A request: the path below the version root and its headers
struct Request {
    QString path;
    HeaderList headers;
};

// The people the user works with, most relevant first; text empty: everyone (address completion)
Request relevant(const QString &text, int top = 8);
// The organisation's directory: names and addresses containing every typed word, as Outlook's
// People search (needs ConsistencyLevel: eventual, which the request carries). fields: $select
Request directory(const QString &text, int top = 8, const QStringList &fields = {});
// The fields directory() selects by default
QStringList directoryFields();
// A user's photo (by id or address) in one of Graph's sizes: 48, 64, 96, 120, 240, 360, 432, 504, 648
QString photoPath(const QString &user, int size = 96);

QList<Person> parseRelevant(const QJsonObject &answer);
QList<Person> parseDirectory(const QJsonObject &answer);
// The first list, then the second, no address twice (any case) and none without an address; at most max
QList<Person> merge(const QList<Person> &first, const QList<Person> &second, int max);

}
