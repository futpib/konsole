/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXSESSIONBRIDGE_H
#define TMUXSESSIONBRIDGE_H

#include <QObject>

namespace Konsole
{

class Session;
class TmuxGateway;
class TmuxController;
class ViewManager;

/**
 * Owns the gateway↔controller lifecycle for a tmux control mode session.
 * Created by ViewManager when a session enters tmux control mode,
 * parented to ViewManager.
 *
 * Self-destructs (via deleteLater) when tmux control mode ends
 * or the gateway session finishes.
 */
class TmuxSessionBridge : public QObject
{
    Q_OBJECT
public:
    TmuxSessionBridge(Session *gatewaySession, ViewManager *viewManager, QObject *parent);
    ~TmuxSessionBridge() override;

private Q_SLOTS:
    void onTmuxControlModeEnded();
    void onGatewaySessionFinished();

private:
    Session *_gatewaySession;       // non-owning
    ViewManager *_viewManager;      // non-owning (same as parent)
    TmuxGateway *_gateway;          // owned (Qt parent = this)
    TmuxController *_controller;    // owned (Qt parent = this)
};

} // namespace Konsole

#endif // TMUXSESSIONBRIDGE_H
