/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxResizeCoordinator.h"

#include "TmuxCommand.h"
#include "TmuxController.h"
#include "TmuxGateway.h"
#include "TmuxLayoutManager.h"
#include "TmuxLayoutParser.h"
#include "TmuxPaneManager.h"

#include "ViewManager.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "terminalDisplay/TerminalFonts.h"
#include "widgets/TabPageWidget.h"
#include "widgets/ViewContainer.h"
#include "widgets/ViewSplitter.h"

#include <QApplication>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcTmuxLayout)

namespace Konsole
{

TmuxResizeCoordinator::TmuxResizeCoordinator(TmuxGateway *gateway, TmuxController *controller, TmuxPaneManager *paneManager, TmuxLayoutManager *layoutManager, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _controller(controller)
    , _paneManager(paneManager)
    , _layoutManager(layoutManager)
    , _viewManager(viewManager)
{
    _resizeTimer.setSingleShot(true);
    _resizeTimer.setInterval(100);
    connect(&_resizeTimer, &QTimer::timeout, this, &TmuxResizeCoordinator::sendClientSize);

    connect(qApp, &QApplication::focusChanged, this, [this]() {
        // When Konsole gains focus, immediately clear layout constraints
        // so the splitter fills the tab while we wait for tmux to respond
        // with a full-size layout.
        TabbedViewContainer *container = _viewManager->activeContainer();
        if (container && container->window() && container->window()->isActiveWindow()) {
            for (int i = 0; i < container->count(); ++i) {
                auto *page = container->tabPageAt(i);
                if (page && page->isConstrained()) {
                    page->clearConstrainedSize();
                }
            }
        }
        _resizeTimer.start();
    });
    connect(viewManager, &ViewManager::activeViewChanged, this, [this]() {
        _resizeTimer.start();
    });
}

void TmuxResizeCoordinator::onPaneViewSizeChanged(bool suppressResize)
{
    if (suppressResize) {
        return;
    }
    _resizeTimer.start();
}

void TmuxResizeCoordinator::onSplitterMoved(ViewSplitter *splitter)
{
    ViewSplitter *topLevel = splitter->getToplevelSplitter();
    TmuxLayoutNode node = TmuxLayoutManager::buildLayoutNode(topLevel, _paneManager);
    QString layoutString = TmuxLayoutParser::serialize(node);

    qCDebug(lcTmuxLayout) << "onSplitterMoved: serialized layout:" << layoutString;

    // Find window ID for this splitter's tab
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return;
    }
    int tabIndex = container->indexOfSplitter(topLevel);

    const auto &windowToTab = _controller->windowToTabIndex();
    int windowId = -1;
    for (auto it = windowToTab.constBegin(); it != windowToTab.constEnd(); ++it) {
        if (it.value() == tabIndex) {
            windowId = it.key();
            break;
        }
    }

    if (windowId < 0) {
        return;
    }

    // Send refresh-client -C first so tmux knows the window size,
    // then select-layout to set the exact pane proportions.
    // If we send select-layout alone, a subsequent refresh-client -C
    // (from the debounced timer) would cause tmux to re-layout from
    // scratch, overriding our custom layout.
    sendClientSize();

    _gateway->sendCommand(TmuxCommand(QStringLiteral("select-layout"))
                              .windowTarget(windowId)
                              .singleQuotedArg(layoutString)
                              .build());
}

void TmuxResizeCoordinator::sendClientSize()
{
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return;
    }

    std::function<QSize(QWidget *)> computeSize = [&](QWidget *widget) -> QSize {
        auto *td = qobject_cast<TerminalDisplay *>(widget);
        if (td) {
            // Report available widget capacity from pixel size, not the current
            // (possibly forced) grid size. This ensures Konsole tells tmux the
            // full size it can display when it regains focus.
            QRect cr = td->contentRect();
            int cols = qBound(1, cr.width() / td->terminalFont()->fontWidth(), 1023);
            int lines = qMax(1, cr.height() / td->terminalFont()->fontHeight());
            return QSize(cols, lines);
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
        sumAxis += sp->count() - 1;

        if (horizontal) {
            return QSize(sumAxis, maxCross);
        } else {
            return QSize(maxCross, sumAxis);
        }
    };

    int activeTabIndex = container->currentIndex();
    bool windowFocused = container->window() && container->window()->isActiveWindow();

    const auto &windowToTab = _controller->windowToTabIndex();
    for (auto it = windowToTab.constBegin(); it != windowToTab.constEnd(); ++it) {
        int windowId = it.key();
        int tabIndex = it.value();

        if (_otherClientsAttached && (!windowFocused || tabIndex != activeTabIndex)) {
            // Clear per-window size for non-active tabs and when unfocused,
            // so other clients can use their own size
            if (_lastClientSizes.contains(windowId)) {
                _lastClientSizes.remove(windowId);
                _gateway->sendCommand(TmuxCommand(QStringLiteral("refresh-client"))
                                          .flag(QStringLiteral("-C"))
                                          .arg(QLatin1Char('@') + QString::number(windowId) + QLatin1Char(':'))
                                          .build());
            }
            continue;
        }

        auto *windowSplitter = container->viewSplitterAt(tabIndex);
        if (!windowSplitter) {
            continue;
        }

        QSize totalSize = computeSize(windowSplitter);
        int totalCols = totalSize.width();
        int totalLines = totalSize.height();

        if (totalCols <= 0 || totalLines <= 0) {
            continue;
        }

        QSize &lastSize = _lastClientSizes[windowId];
        if (totalCols != lastSize.width() || totalLines != lastSize.height()) {
            lastSize = QSize(totalCols, totalLines);
            _gateway->sendCommand(TmuxCommand(QStringLiteral("refresh-client"))
                                      .flag(QStringLiteral("-C"))
                                      .arg(QLatin1Char('@') + QString::number(windowId) + QLatin1Char(':') + QString::number(totalCols) + QLatin1Char('x')
                                           + QString::number(totalLines))
                                      .build());
        }
    }
}

void TmuxResizeCoordinator::setOtherClientsAttached(bool attached)
{
    if (_otherClientsAttached != attached) {
        _otherClientsAttached = attached;
        _resizeTimer.start();
    }
}

void TmuxResizeCoordinator::stop()
{
    _resizeTimer.stop();
}

} // namespace Konsole

#include "moc_TmuxResizeCoordinator.cpp"
