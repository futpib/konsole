/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxResizeCoordinator.h"

#include "TmuxGateway.h"
#include "TmuxLayoutManager.h"

#include "ViewManager.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ViewContainer.h"
#include "widgets/ViewSplitter.h"

#include <QApplication>

namespace Konsole
{

TmuxResizeCoordinator::TmuxResizeCoordinator(TmuxGateway *gateway, TmuxLayoutManager *layoutManager, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _layoutManager(layoutManager)
    , _viewManager(viewManager)
{
    _resizeTimer.setSingleShot(true);
    _resizeTimer.setInterval(100);
    connect(&_resizeTimer, &QTimer::timeout, this, &TmuxResizeCoordinator::sendClientSize);

    connect(qApp, &QApplication::focusChanged, this, [this]() {
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

void TmuxResizeCoordinator::sendClientSize()
{
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return;
    }

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
        sumAxis += sp->count() - 1;

        if (horizontal) {
            return QSize(sumAxis, maxCross);
        } else {
            return QSize(maxCross, sumAxis);
        }
    };

    int activeTabIndex = container->currentIndex();
    bool windowFocused = container->window() && container->window()->isActiveWindow();

    const auto &windowToTab = _layoutManager->windowToTabIndex();
    for (auto it = windowToTab.constBegin(); it != windowToTab.constEnd(); ++it) {
        int windowId = it.key();
        int tabIndex = it.value();

        if (!windowFocused || tabIndex != activeTabIndex) {
            // Clear per-window size for non-active tabs and when unfocused,
            // so other clients can use their own size
            if (_lastClientSizes.contains(windowId)) {
                _lastClientSizes.remove(windowId);
                _gateway->sendCommand(QStringLiteral("refresh-client -C @%1:").arg(windowId));
            }
            continue;
        }

        auto *windowSplitter = qobject_cast<ViewSplitter *>(container->widget(tabIndex));
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
            _gateway->sendCommand(QStringLiteral("refresh-client -C @%1:%2x%3").arg(windowId).arg(totalCols).arg(totalLines));
        }
    }
}

void TmuxResizeCoordinator::stop()
{
    _resizeTimer.stop();
}

} // namespace Konsole

#include "moc_TmuxResizeCoordinator.cpp"
