/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXCONTROLLER_H
#define TMUXCONTROLLER_H

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace Konsole
{

class Session;
class TmuxGateway;
class TmuxLayoutNode;
class ViewManager;
class ViewSplitter;
class TerminalDisplay;

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

class TmuxController : public QObject
{
    Q_OBJECT
public:
    TmuxController(TmuxGateway *gateway, Session *gatewaySession, ViewManager *viewManager, QObject *parent = nullptr);
    ~TmuxController() override;

    void initialize();
    void cleanup();
    void sendClientSize();

    // User-initiated actions that route through tmux
    void requestNewWindow();
    void requestSplitPane(int paneId, Qt::Orientation orientation);
    void requestClosePane(int paneId);
    void requestDetach();

    // Query whether a pane belongs to this controller
    bool hasPane(int paneId) const;
    int paneIdForSession(Session *session) const;

    Session *gatewaySession() const;
    TmuxGateway *gateway() const;

Q_SIGNALS:
    void initialWindowsOpened();
    void detached();

private Q_SLOTS:
    void onOutputReceived(int paneId, const QByteArray &data);
    void onLayoutChanged(int windowId, const QString &layout, const QString &visibleLayout, bool zoomed);
    void onWindowAdded(int windowId);
    void onWindowClosed(int windowId);
    void onWindowRenamed(int windowId, const QString &name);
    void onWindowPaneChanged(int windowId, int paneId);
    void onSessionChanged(int sessionId, const QString &name);
    void onExit(const QString &reason);
    void onPanePaused(int paneId);
    void onPaneContinued(int paneId);

private:
    Session *createPaneSession(int paneId);
    void destroyPaneSession(int paneId);

    void applyLayout(int windowId, const TmuxLayoutNode &layout);
    void buildSplitterTree(ViewSplitter *splitter, const TmuxLayoutNode &node);

    void handleListWindowsResponse(bool success, const QString &response);
    void capturePaneHistory(int paneId);
    void handleCapturePaneResponse(int paneId, bool success, const QString &response);
    bool updateSplitterSizes(ViewSplitter *splitter, const TmuxLayoutNode &node);
    void queryPaneStates(int windowId);
    void handlePaneStateResponse(int windowId, bool success, const QString &response);
    void applyPaneState(int paneId);
    void onPaneViewSizeChanged();
    void connectSplitterSignals(ViewSplitter *splitter);
    void onSplitterMoved(ViewSplitter *splitter);

    TmuxGateway *_gateway;
    Session *_gatewaySession;
    ViewManager *_viewManager;

    QMap<int, Session *> _paneToSession;
    QMap<int, int> _windowToTabIndex;
    QMap<int, QList<int>> _windowPanes;

    QString _sessionName;
    int _sessionId = -1;
    bool _initializing = false;

    QTimer _resizeTimer;

    QHash<int, TmuxPaneState> _paneStates;

    int _lastClientCols = 0;
    int _lastClientLines = 0;

    // Suppress layout size application during user-initiated splitter drags
    int _pendingPaneResizes = 0;

    // Pause mode (tmux 3.2+)
    QSet<int> _pausedPanes;
    QMap<int, QByteArray> _pauseBuffers;
};

} // namespace Konsole

#endif // TMUXCONTROLLER_H
