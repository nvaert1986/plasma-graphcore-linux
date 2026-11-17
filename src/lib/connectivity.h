// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

namespace GraphCore::Connectivity
{

// Whether the system reports a usable network (NetworkManager through QNetworkInformation). When
// the system cannot tell, this says online: a request then finds out (GraphResponse::isNetworkError).
// Call from the main thread first: the backend is loaded there.
bool systemReportsOnline();

// Whether the connection is metered (mobile hotspot); false when unknown
bool isMetered();

// Changes of the two, for a daemon that pauses on a metered connection or syncs at once when the
// network is back. Create on the main thread; the signals only fire when the system can tell.
class Watcher : public QObject
{
    Q_OBJECT

public:
    explicit Watcher(QObject *parent = nullptr);

    bool isOnline() const;
    bool isMetered() const;

Q_SIGNALS:
    void onlineChanged(bool online);
    void meteredChanged(bool metered);

private:
    bool m_online = true;
};

}
