/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxController.h"

#include "TmuxGateway.h"
#include "TmuxLayoutParser.h"

#include "Emulation.h"
#include "ViewManager.h"
#include "profile/ProfileManager.h"
#include "session/Session.h"
#include "session/SessionManager.h"
#include "session/VirtualSession.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ViewContainer.h"
#include "widgets/ViewSplitter.h"

namespace Konsole
{

TmuxController::TmuxController(TmuxGateway *gateway, Session *gatewaySession, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _gatewaySession(gatewaySession)
    , _viewManager(viewManager)
{
    connect(_gateway, &TmuxGateway::outputReceived, this, &TmuxController::onOutputReceived);
    connect(_gateway, &TmuxGateway::layoutChanged, this, &TmuxController::onLayoutChanged);
    connect(_gateway, &TmuxGateway::windowAdded, this, &TmuxController::onWindowAdded);
    connect(_gateway, &TmuxGateway::windowClosed, this, &TmuxController::onWindowClosed);
    connect(_gateway, &TmuxGateway::windowRenamed, this, &TmuxController::onWindowRenamed);
    connect(_gateway, &TmuxGateway::windowPaneChanged, this, &TmuxController::onWindowPaneChanged);
    connect(_gateway, &TmuxGateway::sessionChanged, this, &TmuxController::onSessionChanged);
    connect(_gateway, &TmuxGateway::exitReceived, this, &TmuxController::onExit);
    connect(_gateway, &TmuxGateway::panePaused, this, &TmuxController::onPanePaused);
    connect(_gateway, &TmuxGateway::paneContinued, this, &TmuxController::onPaneContinued);

    // Debounce resize events — multiple panes may resize simultaneously
    _resizeTimer.setSingleShot(true);
    _resizeTimer.setInterval(100);
    connect(&_resizeTimer, &QTimer::timeout, this, &TmuxController::sendClientSize);
}

TmuxController::~TmuxController()
{
    // Clean up all pane sessions
    const auto paneIds = _paneToSession.keys();
    for (int paneId : paneIds) {
        destroyPaneSession(paneId);
    }
}

void TmuxController::initialize()
{
    _initializing = true;
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
    return _paneToSession.contains(paneId);
}

int TmuxController::paneIdForSession(Session *session) const
{
    for (auto it = _paneToSession.constBegin(); it != _paneToSession.constEnd(); ++it) {
        if (it.value() == session) {
            return it.key();
        }
    }
    return -1;
}

void TmuxController::handleListWindowsResponse(bool success, const QString &response)
{
    if (!success || response.isEmpty()) {
        return;
    }

    const QStringList lines = response.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // Each line: "@<id> <name> <layout>"
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
            applyLayout(windowId, parsed.value());
        }
    }

    // Capture pane history for all panes (for reconnection / tmux -CC attach)
    for (auto it = _paneToSession.constBegin(); it != _paneToSession.constEnd(); ++it) {
        capturePaneHistory(it.key());
    }

    _initializing = false;
    Q_EMIT initialWindowsOpened();
}

void TmuxController::capturePaneHistory(int paneId)
{
    // capture-pane -p: print to stdout
    // -J: join wrapped lines
    // -e: include escape sequences (colors/attributes)
    // -t: target pane
    // -S -: start from the beginning of scrollback
    _gateway->sendCommand(QStringLiteral("capture-pane -p -J -e -t %") + QString::number(paneId) + QStringLiteral(" -S -"),
                          [this, paneId](bool success, const QString &response) {
                              handleCapturePaneResponse(paneId, success, response);
                          });
}

void TmuxController::handleCapturePaneResponse(int paneId, bool success, const QString &response)
{
    if (!success || response.isEmpty()) {
        return;
    }

    auto *session = qobject_cast<VirtualSession *>(_paneToSession.value(paneId, nullptr));
    if (!session) {
        return;
    }

    // Inject the captured history line by line
    const QStringList lines = response.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QByteArray lineData = lines[i].toUtf8();
        if (i < lines.size() - 1) {
            lineData.append("\r\n");
        }
        session->injectData(lineData.constData(), lineData.size());
    }
}

Session *TmuxController::createPaneSession(int paneId)
{
    if (_paneToSession.contains(paneId)) {
        return _paneToSession[paneId];
    }

    VirtualSession *session = SessionManager::instance()->createVirtualSession(
        ProfileManager::instance()->defaultProfile());

    connect(session->emulation(), &Emulation::sendData, this, [this, paneId](const QByteArray &data) {
        _gateway->sendKeys(paneId, data);
    });

    connect(session->emulation(), &Emulation::imageSizeChanged, this, &TmuxController::onPaneViewSizeChanged);

    _paneToSession[paneId] = session;
    return session;
}

void TmuxController::destroyPaneSession(int paneId)
{
    auto it = _paneToSession.find(paneId);
    if (it != _paneToSession.end()) {
        Session *session = it.value();
        _paneToSession.erase(it);
        session->close();
        // Don't call deleteLater() here — SessionManager::sessionTerminated()
        // already does that when it receives the finished() signal from close()
    }
}

void TmuxController::onOutputReceived(int paneId, const QByteArray &data)
{
    if (_pausedPanes.contains(paneId)) {
        // Buffer output while paused
        _pauseBuffers[paneId].append(data);
        return;
    }

    auto *session = qobject_cast<VirtualSession *>(_paneToSession.value(paneId, nullptr));
    if (session) {
        session->injectData(data.constData(), data.size());
    }
}

void TmuxController::onPanePaused(int paneId)
{
    _pausedPanes.insert(paneId);
    // Request unpause from tmux
    _gateway->sendCommand(QStringLiteral("refresh-client -A '%") + QString::number(paneId) + QStringLiteral(":on'"));
}

void TmuxController::onPaneContinued(int paneId)
{
    _pausedPanes.remove(paneId);

    // Flush buffered output
    if (_pauseBuffers.contains(paneId)) {
        QByteArray buffered = _pauseBuffers.take(paneId);
        if (!buffered.isEmpty()) {
            auto *session = qobject_cast<VirtualSession *>(_paneToSession.value(paneId, nullptr));
            if (session) {
                session->injectData(buffered.constData(), buffered.size());
            }
        }
    }
}

void TmuxController::onLayoutChanged(int windowId, const QString &layout, const QString &visibleLayout, bool zoomed)
{
    Q_UNUSED(visibleLayout)
    Q_UNUSED(zoomed)

    auto parsed = TmuxLayoutParser::parse(layout);
    if (parsed.has_value()) {
        applyLayout(windowId, parsed.value());
    }
}

void TmuxController::onWindowAdded(int windowId)
{
    // During initialization, list-windows response handles all windows
    if (_initializing) {
        return;
    }
    // Query the window's layout
    _gateway->sendCommand(QStringLiteral("list-windows -t @%1 -F \"#{window_id} #{window_name} #{window_layout}\"").arg(windowId),
                          [this](bool success, const QString &response) {
                              if (!success || response.isEmpty()) {
                                  return;
                              }
                              // Parse the first line
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
                                  applyLayout(wId, parsed.value());
                              }
                          });
}

void TmuxController::onWindowClosed(int windowId)
{
    _windowToTabIndex.remove(windowId);

    if (_windowPanes.contains(windowId)) {
        const QList<int> panes = _windowPanes.take(windowId);
        for (int paneId : panes) {
            destroyPaneSession(paneId);
        }
    }
}

void TmuxController::onWindowRenamed(int windowId, const QString &name)
{
    Q_UNUSED(windowId)
    Q_UNUSED(name)
    // Tab title updates are handled through Session's title mechanism
}

void TmuxController::onWindowPaneChanged(int windowId, int paneId)
{
    Q_UNUSED(windowId)
    Q_UNUSED(paneId)
    // Focus tracking — could be used to switch active pane in the split
}

void TmuxController::onSessionChanged(int sessionId, const QString &name)
{
    _sessionId = sessionId;
    _sessionName = name;
    // Clean up existing windows before re-initializing for the new session
    cleanup();
    initialize();
}

void TmuxController::cleanup()
{
    _resizeTimer.stop();
    _pausedPanes.clear();
    _pauseBuffers.clear();

    const auto paneIds = _paneToSession.keys();
    for (int paneId : paneIds) {
        destroyPaneSession(paneId);
    }
    // Maps are cleared by destroyPaneSession erasing from _paneToSession
    _windowToTabIndex.clear();
    _windowPanes.clear();
}

void TmuxController::onExit(const QString &reason)
{
    Q_UNUSED(reason)

    cleanup();
    Q_EMIT detached();
}

void TmuxController::onPaneViewSizeChanged(int lines, int columns)
{
    Q_UNUSED(lines)
    Q_UNUSED(columns)
    // Debounce: restart the timer on each size change
    _resizeTimer.start();
}

void TmuxController::sendClientSize()
{
    // Compute the maximum columns and lines across all pane views.
    // tmux uses the smallest client size, so we report the size of our
    // overall display area. With a single window visible at a time,
    // use the active tab's top-level splitter size.
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return;
    }

    int maxCols = 0;
    int maxLines = 0;

    // Find the active tmux tab's splitter and use its first terminal's size
    // as a proxy for the client size
    auto *splitter = qobject_cast<ViewSplitter *>(container->currentWidget());
    if (splitter) {
        const auto terminals = splitter->findChildren<TerminalDisplay *>();
        for (const auto *term : terminals) {
            // Get the columns/lines from the terminal
            int cols = term->columns();
            int lines = term->lines();
            if (cols > maxCols) {
                maxCols = cols;
            }
            if (lines > maxLines) {
                maxLines = lines;
            }
        }
    }

    if (maxCols > 0 && maxLines > 0) {
        _gateway->sendCommand(QStringLiteral("refresh-client -C %1,%2").arg(maxCols).arg(maxLines));
    }
}

void TmuxController::applyLayout(int windowId, const TmuxLayoutNode &layout)
{
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return;
    }

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

    // Track panes for this window
    _windowPanes[windowId] = paneIds;

    // Ensure all pane sessions exist
    for (int paneId : paneIds) {
        createPaneSession(paneId);
    }

    if (_windowToTabIndex.contains(windowId)) {
        // Update existing tab
        int tabIndex = _windowToTabIndex[windowId];
        auto *splitter = qobject_cast<ViewSplitter *>(container->widget(tabIndex));
        if (splitter) {
            // Clear existing children and rebuild
            // For now, just rebuild if the layout has multiple panes
            if (layout.type != TmuxLayoutNodeType::Leaf) {
                buildSplitterTree(splitter, layout);
            }
        }
    } else {
        // Create new tab
        auto *splitter = new ViewSplitter();

        if (layout.type == TmuxLayoutNodeType::Leaf) {
            // Single pane — create view and add
            Session *session = _paneToSession[layout.paneId];
            TerminalDisplay *display = _viewManager->createView(session);
            splitter->addTerminalDisplay(display, Qt::Horizontal);
        } else {
            buildSplitterTree(splitter, layout);
        }

        container->addSplitter(splitter);
        int tabIndex = container->indexOf(splitter);
        _windowToTabIndex[windowId] = tabIndex;
    }
}

void TmuxController::buildSplitterTree(ViewSplitter *splitter, const TmuxLayoutNode &node)
{
    if (node.type == TmuxLayoutNodeType::Leaf) {
        Session *session = _paneToSession.value(node.paneId, nullptr);
        if (session) {
            TerminalDisplay *display = _viewManager->createView(session);
            splitter->addTerminalDisplay(display, Qt::Horizontal);
        }
        return;
    }

    Qt::Orientation orientation = (node.type == TmuxLayoutNodeType::HSplit) ? Qt::Horizontal : Qt::Vertical;
    splitter->setOrientation(orientation);

    for (const auto &child : node.children) {
        if (child.type == TmuxLayoutNodeType::Leaf) {
            Session *session = _paneToSession.value(child.paneId, nullptr);
            if (session) {
                TerminalDisplay *display = _viewManager->createView(session);
                splitter->addTerminalDisplay(display, -1);
            }
        } else {
            auto *childSplitter = new ViewSplitter();
            buildSplitterTree(childSplitter, child);
            splitter->addSplitter(childSplitter);
        }
    }

    // Set sizes proportional to the layout dimensions
    QList<int> sizes;
    for (const auto &child : node.children) {
        if (orientation == Qt::Horizontal) {
            sizes.append(child.width);
        } else {
            sizes.append(child.height);
        }
    }
    splitter->setSizes(sizes);
}

} // namespace Konsole
