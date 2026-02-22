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
    QMap<int, QSize> _lastClientSizes;
};

} // namespace Konsole

#endif // TMUXRESIZECOORDINATOR_H
