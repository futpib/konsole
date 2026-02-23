/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxLayoutManager.h"

#include "TmuxLayoutParser.h"
#include "TmuxPaneManager.h"

#include "ViewManager.h"
#include "session/Session.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ViewContainer.h"
#include "widgets/ViewSplitter.h"

namespace Konsole
{

TmuxLayoutManager::TmuxLayoutManager(TmuxPaneManager *paneManager, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _paneManager(paneManager)
    , _viewManager(viewManager)
{
}

int TmuxLayoutManager::applyLayout(int tabIndex, const TmuxLayoutNode &layout)
{
    TabbedViewContainer *container = _viewManager->activeContainer();
    if (!container) {
        return -1;
    }

    if (tabIndex >= 0) {
        auto *oldSplitter = qobject_cast<ViewSplitter *>(container->widget(tabIndex));
        if (oldSplitter) {
            if (!updateSplitterSizes(oldSplitter, layout, false)) {
                // Structure changed — collect existing displays, build new splitter, swap
                QMap<int, TerminalDisplay *> existingDisplays;
                collectDisplays(oldSplitter, existingDisplays);

                // Detach displays we want to reuse from the old splitter tree
                // so they aren't destroyed when the old splitter is deleted
                for (auto *display : existingDisplays) {
                    display->setParent(nullptr);
                }

                // Build a new splitter with the updated layout
                auto *newSplitter = new ViewSplitter();
                newSplitter->setTmuxMode(true);
                buildSplitterTree(newSplitter, layout, existingDisplays);
                connectSplitterSignals(newSplitter);

                // Swap: save tab text, remove old tab, insert new one at same index
                QString tabText = container->tabText(tabIndex);
                QIcon tabIcon = container->tabIcon(tabIndex);
                container->removeTab(tabIndex);
                container->addSplitter(newSplitter, tabIndex);
                container->setTabText(container->indexOf(newSplitter), tabText);
                container->setTabIcon(container->indexOf(newSplitter), tabIcon);
                tabIndex = container->indexOf(newSplitter);

                // The old splitter (and any remaining children like nested splitters)
                // will be deleted. Displays were already detached above.
                // Disconnect to prevent viewDestroyed from firing during deletion.
                oldSplitter->disconnect();
                delete oldSplitter;

                // Destroy leftover displays that are no longer in the layout
                for (auto *display : existingDisplays) {
                    display->deleteLater();
                }
            }
        }
        return tabIndex;
    }

    auto *splitter = new ViewSplitter();
    splitter->setTmuxMode(true);
    QMap<int, TerminalDisplay *> noExisting;

    if (layout.type == TmuxLayoutNodeType::Leaf) {
        Session *session = _paneManager->sessionForPane(layout.paneId);
        TerminalDisplay *display = _viewManager->createView(session);
        splitter->addTerminalDisplay(display, Qt::Horizontal);
    } else {
        buildSplitterTree(splitter, layout, noExisting);
        connectSplitterSignals(splitter);
    }

    container->addSplitter(splitter);
    return container->indexOf(splitter);
}

bool TmuxLayoutManager::updateSplitterSizes(ViewSplitter *splitter, const TmuxLayoutNode &node, bool skipSizeUpdate)
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
            if (!updateSplitterSizes(childSplitter, child, skipSizeUpdate)) {
                return false;
            }
        }
    }

    if (!skipSizeUpdate) {
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

void TmuxLayoutManager::collectDisplays(ViewSplitter *splitter, QMap<int, TerminalDisplay *> &displayMap)
{
    for (int i = 0; i < splitter->count(); ++i) {
        auto *display = qobject_cast<TerminalDisplay *>(splitter->widget(i));
        if (display) {
            int paneId = _paneManager->paneIdForDisplay(display);
            if (paneId >= 0) {
                displayMap[paneId] = display;
            }
        } else if (auto *childSplitter = qobject_cast<ViewSplitter *>(splitter->widget(i))) {
            collectDisplays(childSplitter, displayMap);
        }
    }
}


void TmuxLayoutManager::buildSplitterTree(ViewSplitter *splitter, const TmuxLayoutNode &node, QMap<int, TerminalDisplay *> &existingDisplays)
{
    auto getOrCreateDisplay = [&](int paneId) -> TerminalDisplay * {
        if (existingDisplays.contains(paneId)) {
            return existingDisplays.take(paneId);
        }
        Session *session = _paneManager->sessionForPane(paneId);
        if (session) {
            return _viewManager->createView(session);
        }
        return nullptr;
    };

    if (node.type == TmuxLayoutNodeType::Leaf) {
        TerminalDisplay *display = getOrCreateDisplay(node.paneId);
        if (display) {
            splitter->addTerminalDisplay(display, Qt::Horizontal);
        }
        return;
    }

    Qt::Orientation orientation = (node.type == TmuxLayoutNodeType::HSplit) ? Qt::Horizontal : Qt::Vertical;
    splitter->setOrientation(orientation);

    splitter->setUpdatesEnabled(false);

    for (const auto &child : node.children) {
        if (child.type == TmuxLayoutNodeType::Leaf) {
            TerminalDisplay *display = getOrCreateDisplay(child.paneId);
            if (display) {
                splitter->addTerminalDisplay(display, -1);
            }
        } else {
            auto *childSplitter = new ViewSplitter();
            childSplitter->setTmuxMode(true);
            buildSplitterTree(childSplitter, child, existingDisplays);
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
        Q_EMIT splitterMoved(splitter);
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

} // namespace Konsole

#include "moc_TmuxLayoutManager.cpp"
