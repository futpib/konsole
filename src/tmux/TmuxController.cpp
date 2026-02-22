/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxController.h"

#include "TmuxGateway.h"
#include "TmuxLayoutManager.h"
#include "TmuxLayoutParser.h"
#include "TmuxPaneManager.h"
#include "TmuxPaneStateRecovery.h"
#include "TmuxResizeCoordinator.h"

#include "session/Session.h"

namespace Konsole
{

TmuxController::TmuxController(TmuxGateway *gateway, Session *gatewaySession, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _gatewaySession(gatewaySession)
    , _viewManager(viewManager)
    , _paneManager(new TmuxPaneManager(gateway, this))
    , _layoutManager(new TmuxLayoutManager(gateway, _paneManager, viewManager, this))
    , _resizeCoordinator(new TmuxResizeCoordinator(gateway, _layoutManager, viewManager, this))
    , _stateRecovery(new TmuxPaneStateRecovery(gateway, _paneManager, this))
{
    // Gateway → controller slots
    connect(_gateway, &TmuxGateway::outputReceived, _paneManager, &TmuxPaneManager::deliverOutput);
    connect(_gateway, &TmuxGateway::layoutChanged, this, &TmuxController::onLayoutChanged);
    connect(_gateway, &TmuxGateway::windowAdded, this, &TmuxController::onWindowAdded);
    connect(_gateway, &TmuxGateway::windowClosed, this, &TmuxController::onWindowClosed);
    connect(_gateway, &TmuxGateway::windowRenamed, this, &TmuxController::onWindowRenamed);
    connect(_gateway, &TmuxGateway::windowPaneChanged, this, &TmuxController::onWindowPaneChanged);
    connect(_gateway, &TmuxGateway::sessionChanged, this, &TmuxController::onSessionChanged);
    connect(_gateway, &TmuxGateway::exitReceived, this, &TmuxController::onExit);
    connect(_gateway, &TmuxGateway::panePaused, _paneManager, &TmuxPaneManager::pausePane);
    connect(_gateway, &TmuxGateway::paneContinued, _paneManager, &TmuxPaneManager::continuePane);
    connect(_gateway, &TmuxGateway::clientSessionChanged, this, [this]() {
        refreshClientCount();
    });
    connect(_gateway, &TmuxGateway::clientDetached, this, [this]() {
        refreshClientCount();
    });

    // Pane view size changes → resize coordinator
    connect(_paneManager, &TmuxPaneManager::paneViewSizeChanged, this, [this]() {
        bool suppress = (_state == State::ApplyingLayout || _state == State::Initializing);
        _resizeCoordinator->onPaneViewSizeChanged(suppress);
    });

    // Splitter drag state management
    connect(_layoutManager, &TmuxLayoutManager::splitterDragStarted, this, [this]() {
        _state = State::Dragging;
        _layoutManager->setDragging(true);
    });
    connect(_layoutManager, &TmuxLayoutManager::splitterDragFinished, this, [this]() {
        _state = State::Idle;
        _layoutManager->setDragging(false);
    });
}

TmuxController::~TmuxController()
{
    _paneManager->destroyAllPaneSessions();
}

void TmuxController::initialize()
{
    _state = State::Initializing;
    _gateway->sendCommand(QStringLiteral("list-windows -F \"#{window_id} #{window_name} #{window_layout}\""),
                          [this](bool success, const QString &response) {
                              handleListWindowsResponse(success, response);
                          });
}

Session *TmuxController::gatewaySession() const
{
    return _gatewaySession;
}

TmuxGateway *TmuxController::gateway() const
{
    return _gateway;
}

void TmuxController::requestNewWindow()
{
    _gateway->sendCommand(QStringLiteral("new-window"));
}

void TmuxController::requestSplitPane(int paneId, Qt::Orientation orientation)
{
    QString direction = (orientation == Qt::Horizontal) ? QStringLiteral("-h") : QStringLiteral("-v");
    _gateway->sendCommand(QStringLiteral("split-window ") + direction + QStringLiteral(" -t %") + QString::number(paneId));
}

void TmuxController::requestClosePane(int paneId)
{
    _gateway->sendCommand(QStringLiteral("kill-pane -t %") + QString::number(paneId));
}

void TmuxController::requestDetach()
{
    _gateway->detach();
}

bool TmuxController::hasPane(int paneId) const
{
    return _paneManager->hasPane(paneId);
}

int TmuxController::paneIdForSession(Session *session) const
{
    return _paneManager->paneIdForSession(session);
}

void TmuxController::handleListWindowsResponse(bool success, const QString &response)
{
    if (!success || response.isEmpty()) {
        return;
    }

    // _state is already Initializing (which subsumes ApplyingLayout)
    const QStringList lines = response.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        int firstSpace = line.indexOf(QLatin1Char(' '));
        if (firstSpace < 0) {
            continue;
        }
        int secondSpace = line.indexOf(QLatin1Char(' '), firstSpace + 1);
        if (secondSpace < 0) {
            continue;
        }

        QString windowIdStr = line.left(firstSpace);
        if (!windowIdStr.startsWith(QLatin1Char('@'))) {
            continue;
        }
        int windowId = windowIdStr.mid(1).toInt();
        QString layout = line.mid(secondSpace + 1);

        auto parsed = TmuxLayoutParser::parse(layout);
        if (parsed.has_value()) {
            _layoutManager->applyLayout(windowId, parsed.value());
        }
    }

    // Query pane state for each window before capturing history
    const auto &windowPanes = _layoutManager->windowPanes();
    for (auto it = windowPanes.constBegin(); it != windowPanes.constEnd(); ++it) {
        _stateRecovery->queryPaneStates(it.key());
    }

    // Capture pane history for all panes
    const auto &paneMap = _paneManager->paneToSession();
    for (auto it = paneMap.constBegin(); it != paneMap.constEnd(); ++it) {
        _stateRecovery->capturePaneHistory(it.key());
    }

    _state = State::Idle;
    refreshClientCount();
    Q_EMIT initialWindowsOpened();
}

void TmuxController::onLayoutChanged(int windowId, const QString &layout, const QString &visibleLayout, bool zoomed)
{
    Q_UNUSED(visibleLayout)
    Q_UNUSED(zoomed)

    auto parsed = TmuxLayoutParser::parse(layout);
    if (parsed.has_value()) {
        _state = State::ApplyingLayout;
        _layoutManager->applyLayout(windowId, parsed.value());
        _state = State::Idle;
    }
}

void TmuxController::onWindowAdded(int windowId)
{
    if (_state == State::Initializing) {
        return;
    }
    _gateway->sendCommand(QStringLiteral("list-windows -t @%1 -F \"#{window_id} #{window_name} #{window_layout}\"").arg(windowId),
                          [this](bool success, const QString &response) {
                              if (!success || response.isEmpty()) {
                                  return;
                              }
                              QString line = response.split(QLatin1Char('\n'), Qt::SkipEmptyParts).first();
                              int firstSpace = line.indexOf(QLatin1Char(' '));
                              int secondSpace = line.indexOf(QLatin1Char(' '), firstSpace + 1);
                              if (firstSpace < 0 || secondSpace < 0) {
                                  return;
                              }
                              QString windowIdStr = line.left(firstSpace);
                              int wId = windowIdStr.mid(1).toInt();
                              QString layout = line.mid(secondSpace + 1);
                              auto parsed = TmuxLayoutParser::parse(layout);
                              if (parsed.has_value()) {
                                  _state = State::ApplyingLayout;
                                  _layoutManager->applyLayout(wId, parsed.value());
                                  _state = State::Idle;
                              }
                          });
}

void TmuxController::onWindowClosed(int windowId)
{
    // Destroy pane sessions for this window
    const auto &windowPanes = _layoutManager->windowPanes();
    if (windowPanes.contains(windowId)) {
        const QList<int> panes = windowPanes[windowId];
        for (int paneId : panes) {
            _paneManager->destroyPaneSession(paneId);
        }
    }
    _layoutManager->removeWindow(windowId);
}

void TmuxController::onWindowRenamed(int windowId, const QString &name)
{
    Q_UNUSED(windowId)
    Q_UNUSED(name)
}

void TmuxController::onWindowPaneChanged(int windowId, int paneId)
{
    Q_UNUSED(windowId)
    Q_UNUSED(paneId)
}

void TmuxController::onSessionChanged(int sessionId, const QString &name)
{
    _sessionId = sessionId;
    _sessionName = name;
    cleanup();
    initialize();
}

void TmuxController::cleanup()
{
    _resizeCoordinator->stop();
    _stateRecovery->clear();
    _paneManager->destroyAllPaneSessions();
    _layoutManager->clearAll();
}

void TmuxController::onExit(const QString &reason)
{
    Q_UNUSED(reason)
    cleanup();
    Q_EMIT detached();
}

void TmuxController::sendClientSize()
{
    _resizeCoordinator->sendClientSize();
}

void TmuxController::refreshClientCount()
{
    _gateway->sendCommand(QStringLiteral("list-clients -F \"#{client_name}\""),
                          [this](bool success, const QString &response) {
                              if (!success) {
                                  return;
                              }
                              int clientCount = response.split(QLatin1Char('\n'), Qt::SkipEmptyParts).count();
                              // We are one of the clients, so others = count - 1
                              _resizeCoordinator->setOtherClientsAttached(clientCount > 1);
                          });
}

} // namespace Konsole
