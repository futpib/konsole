/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxLayoutManager.h"

#include "TmuxGateway.h"
#include "TmuxLayoutParser.h"
#include "TmuxPaneManager.h"

#include "ViewManager.h"
#include "session/Session.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ViewContainer.h"
#include "widgets/ViewSplitter.h"

namespace Konsole
{

TmuxLayoutManager::TmuxLayoutManager(TmuxGateway *gateway, TmuxPaneManager *paneManager, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _paneManager(paneManager)
    , _viewManager(viewManager)
{
}

void TmuxLayoutManager::applyLayout(int windowId, const TmuxLayoutNode &layout)
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

    _windowPanes[windowId] = paneIds;

    // Ensure all pane sessions exist
    for (int paneId : paneIds) {
        _paneManager->createPaneSession(paneId);
    }

    if (_windowToTabIndex.contains(windowId)) {
        int tabIndex = _windowToTabIndex[windowId];
        auto *splitter = qobject_cast<ViewSplitter *>(container->widget(tabIndex));
        if (splitter) {
            if (!updateSplitterSizes(splitter, layout)) {
                buildSplitterTree(splitter, layout);
                connectSplitterSignals(splitter);
            }
        }
    } else {
        auto *splitter = new ViewSplitter();

        if (layout.type == TmuxLayoutNodeType::Leaf) {
            Session *session = _paneManager->sessionForPane(layout.paneId);
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

void TmuxLayoutManager::removeWindow(int windowId)
{
    _windowToTabIndex.remove(windowId);
    _windowPanes.remove(windowId);
}

void TmuxLayoutManager::clearAll()
{
    _windowToTabIndex.clear();
    _windowPanes.clear();
}

const QMap<int, int> &TmuxLayoutManager::windowToTabIndex() const
{
    return _windowToTabIndex;
}

const QMap<int, QList<int>> &TmuxLayoutManager::windowPanes() const
{
    return _windowPanes;
}

void TmuxLayoutManager::setDragging(bool dragging)
{
    _dragging = dragging;
}

bool TmuxLayoutManager::updateSplitterSizes(ViewSplitter *splitter, const TmuxLayoutNode &node)
{
    if (node.type == TmuxLayoutNodeType::Leaf) {
        return splitter->count() == 1 && qobject_cast<TerminalDisplay *>(splitter->widget(0));
    }

    Qt::Orientation expected = (node.type == TmuxLayoutNodeType::HSplit) ? Qt::Horizontal : Qt::Vertical;
    if (splitter->orientation() != expected) {
        return false;
    }

    if (splitter->count() != node.children.size()) {
        return false;
    }

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
    }

    return true;
}

void TmuxLayoutManager::buildSplitterTree(ViewSplitter *splitter, const TmuxLayoutNode &node)
{
    if (node.type == TmuxLayoutNodeType::Leaf) {
        Session *session = _paneManager->sessionForPane(node.paneId);
        if (session) {
            TerminalDisplay *display = _viewManager->createView(session);
            splitter->addTerminalDisplay(display, Qt::Horizontal);
        }
        return;
    }

    Qt::Orientation orientation = (node.type == TmuxLayoutNodeType::HSplit) ? Qt::Horizontal : Qt::Vertical;
    splitter->setOrientation(orientation);

    splitter->setUpdatesEnabled(false);

    for (const auto &child : node.children) {
        if (child.type == TmuxLayoutNodeType::Leaf) {
            Session *session = _paneManager->sessionForPane(child.paneId);
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

void TmuxLayoutManager::connectSplitterSignals(ViewSplitter *splitter)
{
    disconnect(splitter, &QSplitter::splitterMoved, this, nullptr);
    connect(splitter, &QSplitter::splitterMoved, this, [this, splitter]() {
        onSplitterMoved(splitter);
    });

    for (int i = 0; i < splitter->count(); ++i) {
        auto *handle = qobject_cast<ViewSplitterHandle *>(splitter->handle(i));
        if (handle) {
            disconnect(handle, &ViewSplitterHandle::dragStarted, this, nullptr);
            disconnect(handle, &ViewSplitterHandle::dragFinished, this, nullptr);
            connect(handle, &ViewSplitterHandle::dragStarted, this, [this]() {
                Q_EMIT splitterDragStarted();
            });
            connect(handle, &ViewSplitterHandle::dragFinished, this, [this]() {
                Q_EMIT splitterDragFinished();
            });
        }
    }

    for (int i = 0; i < splitter->count(); ++i) {
        auto *childSplitter = qobject_cast<ViewSplitter *>(splitter->widget(i));
        if (childSplitter) {
            connectSplitterSignals(childSplitter);
        }
    }
}

void TmuxLayoutManager::onSplitterMoved(ViewSplitter *splitter)
{
    for (int i = 0; i < splitter->count(); ++i) {
        QWidget *widget = splitter->widget(i);

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

        int paneId = -1;
        const auto &paneMap = _paneManager->paneToSession();
        for (auto it = paneMap.constBegin(); it != paneMap.constEnd(); ++it) {
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

} // namespace Konsole

#include "moc_TmuxLayoutManager.cpp"
