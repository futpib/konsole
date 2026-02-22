/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXRESIZECOORDINATOR_H
#define TMUXRESIZECOORDINATOR_H

#include <QObject>
#include <QTimer>

namespace Konsole
{

class TmuxGateway;
class TmuxLayoutManager;
class ViewManager;

class TmuxResizeCoordinator : public QObject
{
    Q_OBJECT
public:
    TmuxResizeCoordinator(TmuxGateway *gateway, TmuxLayoutManager *layoutManager, ViewManager *viewManager, QObject *parent = nullptr);

    void onPaneViewSizeChanged(bool suppressResize);
    void sendClientSize();
    void stop();

private:
    TmuxGateway *_gateway;
    TmuxLayoutManager *_layoutManager;
    ViewManager *_viewManager;

    QTimer _resizeTimer;
    int _lastClientCols = 0;
    int _lastClientLines = 0;
};

} // namespace Konsole

#endif // TMUXRESIZECOORDINATOR_H
