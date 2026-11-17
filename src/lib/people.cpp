// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "people.h"

#include <QJsonArray>
#include <QSet>
#include <QUrl>

namespace GraphCore::People
{

namespace
{
QString encoded(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

// $search takes quoted terms: a quote in the text would end one
QString searchText(QString text)
{
    text.remove(QLatin1Char('"'));
    text.remove(QLatin1Char('\\'));
    return text.simplified();
}
}

Request relevant(const QString &text, int top)
{
    Request request;
    request.path = QStringLiteral("/me/people?$top=%1&$select=id,displayName,scoredEmailAddresses,jobTitle,department").arg(top);
    const QString search = searchText(text);
    if (!search.isEmpty()) {
        request.path += QStringLiteral("&$search=") + encoded(QLatin1Char('"') + search + QLatin1Char('"'));
    }
    return request;
}

QStringList directoryFields()
{
    return {QStringLiteral("id"), QStringLiteral("displayName"), QStringLiteral("mail"), QStringLiteral("userPrincipalName"), QStringLiteral("jobTitle"), QStringLiteral("department")};
}

Request directory(const QString &text, int top, const QStringList &fields)
{
    // No accountEnabled filter: that needs User.Read.All, the suite has User.ReadBasic.All
    const QString search = searchText(text);
    Request request;
    request.path = QStringLiteral("/users?$top=%1&$count=true&$select=%2&$search=").arg(top).arg((fields.isEmpty() ? directoryFields() : fields).join(QLatin1Char(',')))
        + encoded(QStringLiteral("\"displayName:%1\" OR \"mail:%1\"").arg(search));
    request.headers.append({QByteArrayLiteral("ConsistencyLevel"), QByteArrayLiteral("eventual")});
    return request;
}

QString photoPath(const QString &user, int size)
{
    // An address keeps its "@" (Graph takes both forms; this one reads well in logs)
    return QStringLiteral("/users/%1/photos/%2x%2/$value").arg(QString::fromLatin1(QUrl::toPercentEncoding(user, "@"))).arg(size);
}

QList<Person> parseRelevant(const QJsonObject &answer)
{
    QList<Person> people;
    const QJsonArray values = answer.value(QLatin1String("value")).toArray();
    for (const QJsonValue &value : values) {
        const QJsonObject p = value.toObject();
        QStringList emails;
        const QJsonArray addresses = p.value(QLatin1String("scoredEmailAddresses")).toArray();
        for (const QJsonValue &address : addresses) {
            const QString text = address.toObject().value(QLatin1String("address")).toString();
            if (!text.isEmpty()) {
                emails.append(text);
            }
        }
        people.append({p.value(QLatin1String("id")).toString(),
                       p.value(QLatin1String("displayName")).toString(),
                       emails.value(0),
                       emails,
                       p.value(QLatin1String("jobTitle")).toString(),
                       p.value(QLatin1String("department")).toString()});
    }
    return people;
}

QList<Person> parseDirectory(const QJsonObject &answer)
{
    QList<Person> people;
    const QJsonArray values = answer.value(QLatin1String("value")).toArray();
    for (const QJsonValue &value : values) {
        const QJsonObject u = value.toObject();
        // Without a mailbox the sign-in name is the address people know
        QString email = u.value(QLatin1String("mail")).toString();
        if (email.isEmpty()) {
            email = u.value(QLatin1String("userPrincipalName")).toString();
        }
        people.append({u.value(QLatin1String("id")).toString(),
                       u.value(QLatin1String("displayName")).toString(),
                       email,
                       email.isEmpty() ? QStringList() : QStringList{email},
                       u.value(QLatin1String("jobTitle")).toString(),
                       u.value(QLatin1String("department")).toString()});
    }
    return people;
}

QList<Person> merge(const QList<Person> &first, const QList<Person> &second, int max)
{
    QList<Person> result;
    QSet<QString> seen;
    for (const QList<Person> *list : {&first, &second}) {
        for (const Person &person : *list) {
            if (result.size() >= max) {
                return result;
            }
            const QString key = person.email.toLower();
            if (key.isEmpty() || seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            result.append(person);
        }
    }
    return result;
}

}
