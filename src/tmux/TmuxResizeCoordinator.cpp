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

    ViewSplitter *splitter = nullptr;
    auto *cur = qobject_cast<ViewSplitter *>(container->currentWidget());
    if (cur && !cur->findChildren<TerminalDisplay *>().isEmpty()) {
        splitter = cur;
    }
    if (!splitter) {
        const auto &windowToTab = _layoutManager->windowToTabIndex();
        for (auto it = windowToTab.constBegin(); it != windowToTab.constEnd(); ++it) {
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

    QSize totalSize = computeSize(splitter);
    int totalCols = totalSize.width();
    int totalLines = totalSize.height();

    if (totalCols > 0 && totalLines > 0 && (totalCols != _lastClientCols || totalLines != _lastClientLines)) {
        _lastClientCols = totalCols;
        _lastClientLines = totalLines;
        _gateway->sendCommand(QStringLiteral("refresh-client -C %1,%2").arg(totalCols).arg(totalLines));
    }
}

void TmuxResizeCoordinator::stop()
{
    _resizeTimer.stop();
}

} // namespace Konsole

#include "moc_TmuxResizeCoordinator.cpp"
