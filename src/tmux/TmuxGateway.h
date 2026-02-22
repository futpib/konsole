/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXGATEWAY_H
#define TMUXGATEWAY_H

#include <QByteArray>
#include <QObject>
#include <QQueue>

#include <functional>
#include <optional>

#include "TmuxNotification.h"

namespace Konsole
{

class Session;

class TmuxGateway : public QObject
{
    Q_OBJECT
public:
    explicit TmuxGateway(Session *gatewaySession, QObject *parent = nullptr);

    void processLine(const QByteArray &line);

    using CommandCallback = std::function<void(bool success, const QString &response)>;
    void sendCommand(const QString &command, CommandCallback callback = nullptr);
    void sendKeys(int paneId, const QByteArray &data);
    void detach();

    static std::optional<TmuxNotification> parseNotification(const QByteArray &line);

Q_SIGNALS:
    void outputReceived(int paneId, const QByteArray &data);
    void layoutChanged(int windowId, const QString &layout, const QString &visibleLayout, bool zoomed);
    void windowAdded(int windowId);
    void windowClosed(int windowId);
    void windowRenamed(int windowId, const QString &name);
    void windowPaneChanged(int windowId, int paneId);
    void sessionChanged(int sessionId, const QString &name);
    void sessionRenamed(const QString &name);
    void sessionsChanged();
    void sessionWindowChanged(int sessionId, int windowId);
    void panePaused(int paneId);
    void paneContinued(int paneId);
    void clientSessionChanged(const QString &clientName, int sessionId, const QString &sessionName);
    void clientDetached(const QString &clientName);
    void exitReceived(const QString &reason);

private:
    static QByteArray decodeOctalEscapes(const QByteArray &encoded);
    static int parsePaneId(const QByteArray &token);
    static int parseWindowId(const QByteArray &token);
    static int parseSessionId(const QByteArray &token);
    void handleNotification(const QByteArray &line);
    void finishCurrentCommand(bool success);
    void writeToGateway(const QByteArray &data);

    Session *_gatewaySession;

    struct PendingCommand {
        QString command;
        CommandCallback callback;
        QString response;
        int commandId = -1;
    };
    QQueue<PendingCommand> _pendingCommands;
    bool _inResponseBlock = false;
    bool _serverOriginated = false;
    PendingCommand _currentCommand;
};

} // namespace Konsole

#endif // TMUXGATEWAY_H
