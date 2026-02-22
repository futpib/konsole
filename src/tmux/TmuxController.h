/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXCONTROLLER_H
#define TMUXCONTROLLER_H

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
    void onPaneViewSizeChanged(int lines, int columns);

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

    // Pause mode (tmux 3.2+)
    QSet<int> _pausedPanes;
    QMap<int, QByteArray> _pauseBuffers;
};

} // namespace Konsole

#endif // TMUXCONTROLLER_H
