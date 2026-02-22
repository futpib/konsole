/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXLAYOUTMANAGER_H
#define TMUXLAYOUTMANAGER_H

#include <QList>
#include <QMap>
#include <QObject>

namespace Konsole
{

class Session;
class TmuxGateway;
class TmuxLayoutNode;
class TmuxPaneManager;
class ViewManager;
class ViewSplitter;

class TmuxLayoutManager : public QObject
{
    Q_OBJECT
public:
    TmuxLayoutManager(TmuxGateway *gateway, TmuxPaneManager *paneManager, ViewManager *viewManager, QObject *parent = nullptr);

    void applyLayout(int windowId, const TmuxLayoutNode &layout);
    void removeWindow(int windowId);
    void clearAll();

    const QMap<int, int> &windowToTabIndex() const;
    const QMap<int, QList<int>> &windowPanes() const;

    void setDragging(bool dragging);

Q_SIGNALS:
    void splitterDragStarted();
    void splitterDragFinished();

private:
    void buildSplitterTree(ViewSplitter *splitter, const TmuxLayoutNode &node);
    bool updateSplitterSizes(ViewSplitter *splitter, const TmuxLayoutNode &node);
    void connectSplitterSignals(ViewSplitter *splitter);
    void onSplitterMoved(ViewSplitter *splitter);

    TmuxGateway *_gateway;
    TmuxPaneManager *_paneManager;
    ViewManager *_viewManager;

    QMap<int, int> _windowToTabIndex;
    QMap<int, QList<int>> _windowPanes;

    bool _dragging = false;
};

} // namespace Konsole

#endif // TMUXLAYOUTMANAGER_H
