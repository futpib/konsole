/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXPANESTATERECOVERY_H
#define TMUXPANESTATERECOVERY_H

#include <QHash>
#include <QObject>

namespace Konsole
{

class TmuxGateway;
class TmuxPaneManager;

struct TmuxPaneState {
    int paneId = -1;
    bool alternateOn = false;
    int cursorX = 0;
    int cursorY = 0;
    int scrollRegionUpper = 0;
    int scrollRegionLower = -1; // -1 = bottom of screen
    bool cursorVisible = true;
    bool insertMode = false;
    bool appCursorKeys = false;
    bool appKeypad = false;
    bool wrapMode = true;
    bool mouseStandard = false;
    bool mouseButton = false;
    bool mouseAny = false;
    bool mouseSGR = false;
};

class TmuxPaneStateRecovery : public QObject
{
    Q_OBJECT
public:
    TmuxPaneStateRecovery(TmuxGateway *gateway, TmuxPaneManager *paneManager, QObject *parent = nullptr);

    void queryPaneStates(int windowId);
    void capturePaneHistory(int paneId);
    void applyPaneState(int paneId);
    void clear();

private:
    void handlePaneStateResponse(int windowId, bool success, const QString &response);
    void handleCapturePaneResponse(int paneId, bool success, const QString &response);

    TmuxGateway *_gateway;
    TmuxPaneManager *_paneManager;
    QHash<int, TmuxPaneState> _paneStates;
};

} // namespace Konsole

#endif // TMUXPANESTATERECOVERY_H
