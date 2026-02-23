/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXRESIZECOORDINATOR_H
#define TMUXRESIZECOORDINATOR_H

#include <QMap>
#include <QObject>
#include <QSize>
#include <QTimer>

namespace Konsole
{

class TmuxController;
class TmuxGateway;
class TmuxPaneManager;
class ViewManager;
class ViewSplitter;

class TmuxResizeCoordinator : public QObject
{
    Q_OBJECT
public:
    TmuxResizeCoordinator(TmuxGateway *gateway, TmuxController *controller, TmuxPaneManager *paneManager, ViewManager *viewManager, QObject *parent = nullptr);

    void onPaneViewSizeChanged(bool suppressResize);
    void onSplitterMoved(ViewSplitter *splitter);
    void sendClientSize();
    void stop();

    void setOtherClientsAttached(bool attached);

private:
    TmuxGateway *_gateway;
    TmuxController *_controller;
    TmuxPaneManager *_paneManager;
    ViewManager *_viewManager;

    QTimer _resizeTimer;
    QMap<int, QSize> _lastClientSizes;
    bool _otherClientsAttached = false;
};

} // namespace Konsole

#endif // TMUXRESIZECOORDINATOR_H
