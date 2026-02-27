/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxController.h"

#include "TmuxCommand.h"
#include "TmuxGateway.h"
#include "TmuxLayoutManager.h"
#include "TmuxLayoutParser.h"
#include "TmuxPaneManager.h"
#include "TmuxPaneStateRecovery.h"
#include "TmuxResizeCoordinator.h"

#include "ViewManager.h"
#include "session/Session.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ViewContainer.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(KonsoleTmuxController, "konsole.tmux.controller", QtDebugMsg)

namespace Konsole
{

TmuxController::TmuxController(TmuxGateway *gateway, Session *gatewaySession, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _gatewaySession(gatewaySession)
    , _viewManager(viewManager)
    , _paneManager(new TmuxPaneManager(gateway, this))
    , _layoutManager(new TmuxLayoutManager(_paneManager, viewManager, this))
    , _resizeCoordinator(new TmuxResizeCoordinator(gateway, this, _paneManager, _layoutManager, viewManager, this))
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
    connect(_gateway, &TmuxGateway::clientSessionChanged, this, &TmuxController::refreshClientCount);
    connect(_gateway, &TmuxGateway::clientDetached, this, &TmuxController::refreshClientCount);

    // Unsuppress output when pane state recovery completes
    connect(_stateRecovery, &TmuxPaneStateRecovery::paneRecoveryComplete, _paneManager, &TmuxPaneManager::unsuppressOutput);

    // Pane view size changes → resize coordinator
    connect(_paneManager, &TmuxPaneManager::paneViewSizeChanged, this, [this]() {
        _resizeCoordinator->onPaneViewSizeChanged(shouldSuppressResize());
    });

    // Splitter drag state management
    connect(_layoutManager, &TmuxLayoutManager::splitterDragStarted, this, [this]() {
        qCDebug(KonsoleTmuxController) << "splitterDragStarted signal received";
        setState(State::Dragging);
    });
    connect(_layoutManager, &TmuxLayoutManager::splitterDragFinished, this, [this]() {
        qCDebug(KonsoleTmuxController) << "splitterDragFinished signal received";
        setState(State::Idle);
    });

    // Splitter moved → send per-pane resize commands
    connect(_layoutManager, &TmuxLayoutManager::splitterMoved, _resizeCoordinator, &TmuxResizeCoordinator::onSplitterMoved);

    // Periodic pane title refresh
    _paneTitleTimer = new QTimer(this);
    _paneTitleTimer->setInterval(2000);
    connect(_paneTitleTimer, &QTimer::timeout, this, &TmuxController::refreshPaneTitles);
}

TmuxController::~TmuxController()
{
    _paneManager->destroyAllPaneSessions();
}

void TmuxController::initialize()
{
    setState(State::Initializing);
    _gateway->sendCommand(TmuxCommand(QStringLiteral("list-windows"))
                              .format(QStringLiteral("#{window_id} #{window_name} #{window_layout}")),
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
    _gateway->sendCommand(TmuxCommand(QStringLiteral("new-window")));
}

void TmuxController::requestSplitPane(int paneId, Qt::Orientation orientation)
{
    QString direction = (orientation == Qt::Horizontal) ? QStringLiteral("-h") : QStringLiteral("-v");
    _gateway->sendCommand(TmuxCommand(QStringLiteral("split-window"))
                              .flag(direction)
                              .paneTarget(paneId));
}

void TmuxController::requestClosePane(int paneId)
{
    _gateway->sendCommand(TmuxCommand(QStringLiteral("kill-pane")).paneTarget(paneId));
}

void TmuxController::requestCloseWindow(int windowId)
{
    _gateway->sendCommand(TmuxCommand(QStringLiteral("kill-window")).windowTarget(windowId));
}

void TmuxController::requestSwapPane(int srcPaneId, int dstPaneId)
{
    _gateway->sendCommand(TmuxCommand(QStringLiteral("swap-pane")).paneSource(srcPaneId).paneTarget(dstPaneId));
}

void TmuxController::requestMovePane(int srcPaneId, int dstPaneId, Qt::Orientation orientation, bool before)
{
    TmuxCommand cmd(QStringLiteral("move-pane"));
    cmd.paneSource(srcPaneId).paneTarget(dstPaneId);
    if (orientation == Qt::Horizontal) {
        cmd.flag(QStringLiteral("-h"));
    }
    if (before) {
        cmd.flag(QStringLiteral("-b"));
    }
    _gateway->sendCommand(cmd);
}

void TmuxController::requestRenameWindow(int windowId, const QString &name)
{
    _gateway->sendCommand(TmuxCommand(QStringLiteral("rename-window")).windowTarget(windowId).quotedArg(name));
}

void TmuxController::requestClearHistory(Session *session)
{
    int paneId = _paneManager->paneIdForSession(session);
    if (paneId >= 0) {
        _gateway->sendCommand(TmuxCommand(QStringLiteral("clear-history")).paneTarget(paneId));
    }
}

void TmuxController::requestClearHistoryAndReset(Session *session)
{
    int paneId = _paneManager->paneIdForSession(session);
    if (paneId >= 0) {
        // send-keys -R resets terminal state and C-l clears the visible screen,
        // then clear-history removes the scrollback that was pushed off-screen.
        _gateway->sendCommand(TmuxCommand(QStringLiteral("send-keys")).flag(QStringLiteral("-R")).paneTarget(paneId).arg(QStringLiteral("C-l")));
        _gateway->sendCommand(TmuxCommand(QStringLiteral("clear-history")).paneTarget(paneId));
    }
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

int TmuxController::windowIdForPane(int paneId) const
{
    for (auto it = _windowPanes.constBegin(); it != _windowPanes.constEnd(); ++it) {
        if (it.value().contains(paneId)) {
            return it.key();
        }
    }
    return -1;
}

int TmuxController::windowCount() const
{
    return _windowToTabIndex.size();
}

int TmuxController::paneCountForWindow(int windowId) const
{
    return _windowPanes.value(windowId).size();
}

const QMap<int, int> &TmuxController::windowToTabIndex() const
{
    return _windowToTabIndex;
}

void TmuxController::applyWindowLayout(int windowId, const TmuxLayoutNode &layout)
{
    // Collect pane IDs from the layout tree
    QList<int> paneIds;
    std::function<void(const TmuxLayoutNode &)> collectPanes = [&](const TmuxLayoutNode &node) {
        if (node.type == TmuxLayoutNodeType::Leaf) {
            paneIds.append(node.paneId);
        } else {
            for (const auto &child : node.children) {
                collectPanes(child);
            }
        }
    };
    collectPanes(layout);

    QList<int> oldPaneIds = _windowPanes.value(windowId);
    _windowPanes[windowId] = paneIds;

    // Ensure all pane sessions exist
    for (int paneId : paneIds) {
        _paneManager->createPaneSession(paneId);
    }

    int tabIndex = _windowToTabIndex.value(windowId, -1);
    int newTabIndex = _layoutManager->applyLayout(tabIndex, layout);
    if (newTabIndex >= 0) {
        _windowToTabIndex[windowId] = newTabIndex;
    }

    // Destroy pane sessions for panes removed from this window
    if (tabIndex >= 0) {
        for (int oldPaneId : oldPaneIds) {
            if (!paneIds.contains(oldPaneId)) {
                _paneManager->destroyPaneSession(oldPaneId);
            }
        }
    }
}

void TmuxController::setWindowTabTitle(int windowId, const QString &name)
{
    int tabIndex = _windowToTabIndex.value(windowId, -1);
    if (tabIndex < 0) {
        return;
    }
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (container) {
        container->setTabText(tabIndex, name);
    }
}

void TmuxController::refreshPaneTitles()
{
    _paneManager->queryPaneTitleInfo();
}

void TmuxController::handleListWindowsResponse(bool success, const QString &response)
{
    if (!success || response.isEmpty()) {
        return;
    }

    // Helper to collect leaf pane dimensions from layout tree
    std::function<void(const TmuxLayoutNode &)> collectPaneDimensions = [&](const TmuxLayoutNode &node) {
        if (node.type == TmuxLayoutNodeType::Leaf) {
            _stateRecovery->setPaneDimensions(node.paneId, node.width, node.height);
        } else {
            for (const auto &child : node.children) {
                collectPaneDimensions(child);
            }
        }
    };

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
        QString windowName = line.mid(firstSpace + 1, secondSpace - firstSpace - 1);
        QString layout = line.mid(secondSpace + 1);

        auto parsed = TmuxLayoutParser::parse(layout);
        if (parsed.has_value()) {
            collectPaneDimensions(parsed.value());
            applyWindowLayout(windowId, parsed.value());
            setWindowTabTitle(windowId, windowName);
        }
    }

    // Query pane state for each window before capturing history
    for (auto it = _windowPanes.constBegin(); it != _windowPanes.constEnd(); ++it) {
        _stateRecovery->queryPaneStates(it.key());
    }

    // Suppress %output delivery and capture pane history for all panes.
    // %output arriving during capture would mix ANSI-escaped terminal output
    // with the plain-text capture-pane content, producing garbled display.
    _paneManager->suppressAllOutput();
    const auto paneIds = _paneManager->allPaneIds();
    for (int paneId : paneIds) {
        _stateRecovery->capturePaneHistory(paneId);
    }

    setState(State::Idle);
    refreshClientCount();
    refreshPaneTitles();
    _paneTitleTimer->start();
    Q_EMIT initialWindowsOpened();
}

void TmuxController::onLayoutChanged(int windowId, const QString &layout, const QString &visibleLayout, bool zoomed)
{
    Q_UNUSED(visibleLayout)
    Q_UNUSED(zoomed)

    qCDebug(KonsoleTmuxController) << "onLayoutChanged: windowId=" << windowId << "layout=" << layout << "state=" << static_cast<int>(_state);

    auto parsed = TmuxLayoutParser::parse(layout);
    if (parsed.has_value()) {
        // Always track the actual tmux window size, even during drag —
        // onSplitterMoved needs to know the real window dimensions so
        // select-layout doesn't exceed the tmux window size.
        _resizeCoordinator->setWindowSize(windowId, parsed.value().width, parsed.value().height);
    }

    // Skip layout-change notifications while dragging a splitter — they are
    // echo-backs of our own select-layout commands and would create a feedback
    // loop (select-layout → %layout-change → applyLayout → setSizes →
    // splitterMoved → select-layout …).
    if (_state == State::Dragging) {
        qCDebug(KonsoleTmuxController) << "onLayoutChanged: SKIPPING layout apply — state is Dragging";
        return;
    }

    if (parsed.has_value()) {
        setState(State::ApplyingLayout);
        applyWindowLayout(windowId, parsed.value());
        setState(State::Idle);

        // After applying a layout, (re-)focus the active pane if it belongs
        // to this window. This handles two cases:
        // 1. %window-pane-changed arrived before %layout-change — the pane
        //    session didn't exist yet, so we deferred focus.
        // 2. A subsequent %layout-change rebuilt the splitter tree, destroying
        //    the previously focused display — we need to focus the new one.
        if (_activePaneId >= 0 && _windowPanes.value(windowId).contains(_activePaneId)) {
            focusPane(_activePaneId);
        }
    }
}

void TmuxController::onWindowAdded(int windowId)
{
    if (_state == State::Initializing) {
        return;
    }
    _gateway->sendCommand(TmuxCommand(QStringLiteral("list-windows"))
                              .windowTarget(windowId)
                              .format(QStringLiteral("#{window_id} #{window_name} #{window_layout}")),
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
                              QString windowName = line.mid(firstSpace + 1, secondSpace - firstSpace - 1);
                              QString layout = line.mid(secondSpace + 1);
                              auto parsed = TmuxLayoutParser::parse(layout);
                              if (parsed.has_value()) {
                                  setState(State::ApplyingLayout);
                                  applyWindowLayout(wId, parsed.value());
                                  setWindowTabTitle(wId, windowName);
                                  setState(State::Idle);
                              }
                          });
}

void TmuxController::onWindowClosed(int windowId)
{
    // Destroy pane sessions for this window
    if (_windowPanes.contains(windowId)) {
        const QList<int> panes = _windowPanes[windowId];
        for (int paneId : panes) {
            _paneManager->destroyPaneSession(paneId);
        }
    }
    _windowToTabIndex.remove(windowId);
    _windowPanes.remove(windowId);
}

void TmuxController::onWindowRenamed(int windowId, const QString &name)
{
    setWindowTabTitle(windowId, name);
    _paneManager->queryPaneTitleInfo();
}

void TmuxController::onWindowPaneChanged(int windowId, int paneId)
{
    Q_UNUSED(windowId)
    _activePaneId = paneId;
    focusPane(paneId);
}

bool TmuxController::focusPane(int paneId)
{
    Session *session = _paneManager->sessionForPane(paneId);
    if (!session) {
        return false;
    }
    const auto displays = session->views();
    if (!displays.isEmpty()) {
        displays.first()->setFocus(Qt::OtherFocusReason);
        return true;
    }
    return false;
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
    _paneTitleTimer->stop();
    _resizeCoordinator->stop();
    _stateRecovery->clear();
    _paneManager->destroyAllPaneSessions();
    _windowToTabIndex.clear();
    _windowPanes.clear();
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

void TmuxController::setState(State newState)
{
    static const char *stateNames[] = {"Idle", "Initializing", "ApplyingLayout", "Dragging"};
    qCDebug(KonsoleTmuxController) << "setState:" << stateNames[static_cast<int>(_state)] << "→" << stateNames[static_cast<int>(newState)];
    _state = newState;
}

bool TmuxController::shouldSuppressResize() const
{
    return _state == State::ApplyingLayout || _state == State::Initializing;
}

void TmuxController::refreshClientCount()
{
    _gateway->sendCommand(TmuxCommand(QStringLiteral("list-clients"))
                              .format(QStringLiteral("#{client_name}")),
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
