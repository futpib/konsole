/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXCONTROLLER_H
#define TMUXCONTROLLER_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QTimer>

#include "konsoleprivate_export.h"

namespace Konsole
{

class Session;
class TmuxGateway;
class TmuxLayoutNode;
class TmuxPaneManager;
class TmuxLayoutManager;
class TmuxResizeCoordinator;
class TmuxPaneStateRecovery;
class ViewManager;

class KONSOLEPRIVATE_EXPORT TmuxController : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Initializing, ApplyingLayout, Dragging };

    TmuxController(TmuxGateway *gateway, Session *gatewaySession, ViewManager *viewManager, QObject *parent = nullptr);
    ~TmuxController() override;

    void initialize();
    void cleanup();
    void sendClientSize();

    void requestNewWindow();
    void requestSplitPane(int paneId, Qt::Orientation orientation);
    void requestClosePane(int paneId);
    void requestCloseWindow(int windowId);
    void requestSwapPane(int srcPaneId, int dstPaneId);
    void requestMovePane(int srcPaneId, int dstPaneId, Qt::Orientation orientation, bool before);
    void requestRenameWindow(int windowId, const QString &name);
    void requestClearHistory(Session *session);
    void requestDetach();

    bool hasPane(int paneId) const;
    int paneIdForSession(Session *session) const;
    int windowIdForPane(int paneId) const;
    int windowCount() const;
    int paneCountForWindow(int windowId) const;

    const QMap<int, int> &windowToTabIndex() const;

    Session *gatewaySession() const;
    TmuxGateway *gateway() const;

Q_SIGNALS:
    void initialWindowsOpened();
    void detached();

private Q_SLOTS:
    void onLayoutChanged(int windowId, const QString &layout, const QString &visibleLayout, bool zoomed);
    void onWindowAdded(int windowId);
    void onWindowClosed(int windowId);
    void onWindowRenamed(int windowId, const QString &name);
    void onWindowPaneChanged(int windowId, int paneId);
    void onSessionChanged(int sessionId, const QString &name);
    void onExit(const QString &reason);

private:
    void setState(State newState);
    bool shouldSuppressResize() const;

    void applyWindowLayout(int windowId, const TmuxLayoutNode &layout);
    void setWindowTabTitle(int windowId, const QString &name);
    void refreshPaneTitles();
    void handleListWindowsResponse(bool success, const QString &response);
    void refreshClientCount();

    TmuxGateway *_gateway;
    Session *_gatewaySession;
    ViewManager *_viewManager;

    TmuxPaneManager *_paneManager;
    TmuxLayoutManager *_layoutManager;
    TmuxResizeCoordinator *_resizeCoordinator;
    TmuxPaneStateRecovery *_stateRecovery;

    QMap<int, int> _windowToTabIndex;
    QMap<int, QList<int>> _windowPanes;

    QTimer *_paneTitleTimer;

    QString _sessionName;
    int _sessionId = -1;
    State _state = State::Idle;
};

} // namespace Konsole

#endif // TMUXCONTROLLER_H
