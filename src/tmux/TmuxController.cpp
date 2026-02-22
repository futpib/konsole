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
#include "terminalDisplay/TerminalFonts.h"
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

    _applyingLayout = true;
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
    _applyingLayout = false;

    // Query pane state for each window before capturing history
    for (auto it = _windowPanes.constBegin(); it != _windowPanes.constEnd(); ++it) {
        queryPaneStates(it.key());
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

    // Apply terminal state (cursor position, modes, etc.) after content
    applyPaneState(paneId);
}

void TmuxController::queryPaneStates(int windowId)
{
    static const QString format = QStringLiteral(
        "#{pane_id}\t#{alternate_on}\t#{cursor_x}\t#{cursor_y}"
        "\t#{scroll_region_upper}\t#{scroll_region_lower}"
        "\t#{cursor_flag}\t#{insert_flag}\t#{keypad_cursor_flag}"
        "\t#{keypad_flag}\t#{wrap_flag}\t#{mouse_standard_flag}"
        "\t#{mouse_button_flag}\t#{mouse_any_flag}\t#{mouse_sgr_flag}");

    _gateway->sendCommand(QStringLiteral("list-panes -t @%1 -F \"%2\"").arg(windowId).arg(format),
                          [this, windowId](bool success, const QString &response) {
                              handlePaneStateResponse(windowId, success, response);
                          });
}

void TmuxController::handlePaneStateResponse(int windowId, bool success, const QString &response)
{
    Q_UNUSED(windowId)

    if (!success || response.isEmpty()) {
        return;
    }

    const QStringList lines = response.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 15) {
            continue;
        }

        // Parse pane ID from %<id>
        QString paneIdStr = fields[0];
        if (!paneIdStr.startsWith(QLatin1Char('%'))) {
            continue;
        }
        int paneId = paneIdStr.mid(1).toInt();

        TmuxPaneState state;
        state.paneId = paneId;
        state.alternateOn = fields[1] == QLatin1String("1");
        state.cursorX = fields[2].toInt();
        state.cursorY = fields[3].toInt();
        state.scrollRegionUpper = fields[4].toInt();
        state.scrollRegionLower = fields[5].toInt();
        state.cursorVisible = fields[6] == QLatin1String("1");
        state.insertMode = fields[7] == QLatin1String("1");
        state.appCursorKeys = fields[8] == QLatin1String("1");
        state.appKeypad = fields[9] == QLatin1String("1");
        state.wrapMode = fields[10] == QLatin1String("1");
        state.mouseStandard = fields[11] == QLatin1String("1");
        state.mouseButton = fields[12] == QLatin1String("1");
        state.mouseAny = fields[13] == QLatin1String("1");
        state.mouseSGR = fields[14] == QLatin1String("1");

        _paneStates[paneId] = state;
    }
}

void TmuxController::applyPaneState(int paneId)
{
    if (!_paneStates.contains(paneId)) {
        return;
    }

    auto *session = qobject_cast<VirtualSession *>(_paneToSession.value(paneId, nullptr));
    if (!session) {
        return;
    }

    const TmuxPaneState &state = _paneStates[paneId];
    QByteArray seq;

    // Switch to alternate screen if active
    if (state.alternateOn) {
        seq.append("\033[?1049h");
    }

    // Set scroll region (DECSTBM, 1-indexed)
    if (state.scrollRegionUpper != 0 || state.scrollRegionLower != -1) {
        int lower = state.scrollRegionLower;
        if (lower < 0) {
            // Use a large value; the terminal will clamp to screen height
            lower = 9999;
        }
        seq.append(QStringLiteral("\033[%1;%2r").arg(state.scrollRegionUpper + 1).arg(lower + 1).toUtf8());
    }

    // Set cursor position (CUP, 1-indexed)
    seq.append(QStringLiteral("\033[%1;%2H").arg(state.cursorY + 1).arg(state.cursorX + 1).toUtf8());

    // Cursor visibility
    if (!state.cursorVisible) {
        seq.append("\033[?25l");
    }

    // Insert mode
    if (state.insertMode) {
        seq.append("\033[4h");
    }

    // Application cursor keys
    if (state.appCursorKeys) {
        seq.append("\033[?1h");
    }

    // Application keypad
    if (state.appKeypad) {
        seq.append("\033=");
    }

    // Wrap mode off (default is on)
    if (!state.wrapMode) {
        seq.append("\033[?7l");
    }

    // Mouse modes (only enable active ones)
    if (state.mouseStandard) {
        seq.append("\033[?1000h");
    }
    if (state.mouseButton) {
        seq.append("\033[?1002h");
    }
    if (state.mouseAny) {
        seq.append("\033[?1003h");
    }
    if (state.mouseSGR) {
        seq.append("\033[?1006h");
    }

    if (!seq.isEmpty()) {
        session->injectData(seq.constData(), seq.size());
    }

    // Clean up — state has been applied
    _paneStates.remove(paneId);
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

    connect(session->emulation(), &Emulation::imageSizeChanged, this, [this](int, int) {
        onPaneViewSizeChanged();
    });

    connect(session, &QObject::destroyed, this, [this, paneId]() {
        _paneToSession.remove(paneId);
    });

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
        _applyingLayout = true;
        applyLayout(windowId, parsed.value());
        _applyingLayout = false;
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
                                  _applyingLayout = true;
                                  applyLayout(wId, parsed.value());
                                  _applyingLayout = false;
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
    _paneStates.clear();
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

void TmuxController::onPaneViewSizeChanged()
{
    if (_applyingLayout) {
        return;
    }
    // Debounce: restart the timer on each size change
    _resizeTimer.start();
}

void TmuxController::sendClientSize()
{
    // tmux's refresh-client -C expects the total client area: the sum of
    // pane character sizes plus dividers, matching the tmux layout model.
    // We walk the splitter tree to compute this correctly, accounting for
    // scrollbars, margins, and other per-terminal overhead.
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return;
    }

    // Find any tmux tab's splitter
    ViewSplitter *splitter = nullptr;
    auto *cur = qobject_cast<ViewSplitter *>(container->currentWidget());
    if (cur && !cur->findChildren<TerminalDisplay *>().isEmpty()) {
        splitter = cur;
    }
    if (!splitter) {
        for (auto it = _windowToTabIndex.constBegin(); it != _windowToTabIndex.constEnd(); ++it) {
            auto *s = qobject_cast<ViewSplitter *>(container->widget(it.value()));
            if (s) {
                splitter = s;
                break;
            }
        }
    }
    if (!splitter) {
        return;
    }

    // Recursively compute character-cell size of the splitter tree
    // like tmux would: sum along split axis, max along the other axis,
    // +1 for each divider.
    std::function<QSize(QWidget *)> computeSize = [&](QWidget *widget) -> QSize {
        auto *td = qobject_cast<TerminalDisplay *>(widget);
        if (td) {
            return QSize(td->columns(), td->lines());
        }
        auto *sp = qobject_cast<ViewSplitter *>(widget);
        if (!sp || sp->count() == 0) {
            return QSize(0, 0);
        }
        if (sp->count() == 1) {
            return computeSize(sp->widget(0));
        }

        bool horizontal = (sp->orientation() == Qt::Horizontal);
        int sumAxis = 0;
        int maxCross = 0;
        for (int i = 0; i < sp->count(); ++i) {
            QSize childSize = computeSize(sp->widget(i));
            if (horizontal) {
                sumAxis += childSize.width();
                maxCross = qMax(maxCross, childSize.height());
            } else {
                sumAxis += childSize.height();
                maxCross = qMax(maxCross, childSize.width());
            }
        }
        // Add 1 character for each divider between children
        sumAxis += sp->count() - 1;

        if (horizontal) {
            return QSize(sumAxis, maxCross);
        } else {
            return QSize(maxCross, sumAxis);
        }
    };

    QSize totalSize = computeSize(splitter);
    int totalCols = totalSize.width();
    int totalLines = totalSize.height();


    if (totalCols > 0 && totalLines > 0 && (totalCols != _lastClientCols || totalLines != _lastClientLines)) {
        _lastClientCols = totalCols;
        _lastClientLines = totalLines;
        _gateway->sendCommand(QStringLiteral("refresh-client -C %1,%2").arg(totalCols).arg(totalLines));
    }
}

void TmuxController::connectSplitterSignals(ViewSplitter *splitter)
{
    disconnect(splitter, &QSplitter::splitterMoved, this, nullptr);
    connect(splitter, &QSplitter::splitterMoved, this, [this, splitter]() {
        onSplitterMoved(splitter);
    });

    // Connect handle drag signals for each handle in this splitter
    for (int i = 0; i < splitter->count(); ++i) {
        auto *handle = qobject_cast<ViewSplitterHandle *>(splitter->handle(i));
        if (handle) {
            disconnect(handle, &ViewSplitterHandle::dragStarted, this, nullptr);
            disconnect(handle, &ViewSplitterHandle::dragFinished, this, nullptr);
            connect(handle, &ViewSplitterHandle::dragStarted, this, [this]() {
                _dragging = true;
            });
            connect(handle, &ViewSplitterHandle::dragFinished, this, [this]() {
                _dragging = false;
            });
        }
    }

    // Recurse into child splitters
    for (int i = 0; i < splitter->count(); ++i) {
        auto *childSplitter = qobject_cast<ViewSplitter *>(splitter->widget(i));
        if (childSplitter) {
            connectSplitterSignals(childSplitter);
        }
    }
}

void TmuxController::onSplitterMoved(ViewSplitter *splitter)
{

    // Send absolute resize for every pane in this splitter
    for (int i = 0; i < splitter->count(); ++i) {
        QWidget *widget = splitter->widget(i);

        // Drill down to find a leaf TerminalDisplay
        while (auto *sub = qobject_cast<ViewSplitter *>(widget)) {
            if (sub->count() == 0) {
                break;
            }
            widget = sub->widget(0);
        }

        auto *display = qobject_cast<TerminalDisplay *>(widget);
        if (!display) {
            continue;
        }

        // Find the pane ID for this display
        int paneId = -1;
        for (auto it = _paneToSession.constBegin(); it != _paneToSession.constEnd(); ++it) {
            if (it.value()->views().contains(display)) {
                paneId = it.key();
                break;
            }
        }
        if (paneId < 0) {
            continue;
        }

        int cols = display->columns();
        int lines = display->lines();
        if (cols > 0 && lines > 0) {
            QString cmd = QStringLiteral("resize-pane -t %") + QString::number(paneId)
                + QStringLiteral(" -x ") + QString::number(cols)
                + QStringLiteral(" -y ") + QString::number(lines);
            _gateway->sendCommand(cmd);
        }
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
        // Update existing tab — try to just update sizes if structure matches
        int tabIndex = _windowToTabIndex[windowId];
        auto *splitter = qobject_cast<ViewSplitter *>(container->widget(tabIndex));
        if (splitter) {
            if (!updateSplitterSizes(splitter, layout)) {
                // Structure changed — full rebuild needed
                buildSplitterTree(splitter, layout);
                connectSplitterSignals(splitter);
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
            connectSplitterSignals(splitter);
        }

        container->addSplitter(splitter);
        int tabIndex = container->indexOf(splitter);
        _windowToTabIndex[windowId] = tabIndex;
    }
}

bool TmuxController::updateSplitterSizes(ViewSplitter *splitter, const TmuxLayoutNode &node)
{
    if (node.type == TmuxLayoutNodeType::Leaf) {
        // A leaf matches if the splitter has exactly one TerminalDisplay child
        // (which is how addView / buildSplitterTree creates single-pane tabs)
        return splitter->count() == 1 && qobject_cast<TerminalDisplay *>(splitter->widget(0));
    }

    Qt::Orientation expected = (node.type == TmuxLayoutNodeType::HSplit) ? Qt::Horizontal : Qt::Vertical;
    if (splitter->orientation() != expected) {
        return false;
    }

    if (splitter->count() != node.children.size()) {
        return false;
    }

    // Check each child matches the expected type
    for (int i = 0; i < node.children.size(); ++i) {
        const auto &child = node.children[i];
        QWidget *widget = splitter->widget(i);

        if (child.type == TmuxLayoutNodeType::Leaf) {
            if (!qobject_cast<TerminalDisplay *>(widget)) {
                return false;
            }
        } else {
            auto *childSplitter = qobject_cast<ViewSplitter *>(widget);
            if (!childSplitter) {
                return false;
            }
            if (!updateSplitterSizes(childSplitter, child)) {
                return false;
            }
        }
    }

    // Structure matches — apply tmux's proportions so that tmux-initiated
    // resizes (e.g. window resize, other client resize) are reflected.
    // Skip during user-initiated splitter drags to avoid fighting the user.
    if (!_dragging) {
        QList<int> sizes;
        for (const auto &child : node.children) {
            if (splitter->orientation() == Qt::Horizontal) {
                sizes.append(child.width);
            } else {
                sizes.append(child.height);
            }
        }
        splitter->setSizes(sizes);
    } else {
    }

    return true;
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

    // Block updates while building the tree to prevent resize events on
    // partially-constructed displays (Screen buffer may not be sized yet)
    splitter->setUpdatesEnabled(false);

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

    splitter->setUpdatesEnabled(true);
}

} // namespace Konsole
