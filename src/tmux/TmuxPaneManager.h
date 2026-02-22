/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXPANEMANAGER_H
#define TMUXPANEMANAGER_H

#include <QMap>
#include <QObject>
#include <QSet>

namespace Konsole
{

class Session;
class TmuxGateway;

class TmuxPaneManager : public QObject
{
    Q_OBJECT
public:
    explicit TmuxPaneManager(TmuxGateway *gateway, QObject *parent = nullptr);

    Session *createPaneSession(int paneId);
    void destroyPaneSession(int paneId);
    void destroyAllPaneSessions();

    void deliverOutput(int paneId, const QByteArray &data);
    void pausePane(int paneId);
    void continuePane(int paneId);

    bool hasPane(int paneId) const;
    int paneIdForSession(Session *session) const;
    Session *sessionForPane(int paneId) const;
    const QMap<int, Session *> &paneToSession() const;

Q_SIGNALS:
    void paneViewSizeChanged();

private:
    TmuxGateway *_gateway;
    QMap<int, Session *> _paneToSession;
    QSet<int> _pausedPanes;
    QMap<int, QByteArray> _pauseBuffers;
};

} // namespace Konsole

#endif // TMUXPANEMANAGER_H
