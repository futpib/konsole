/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxSessionBridge.h"

#include "TmuxController.h"
#include "TmuxControllerRegistry.h"
#include "TmuxGateway.h"

#include "Vt102Emulation.h"
#include "session/Session.h"

namespace Konsole
{

TmuxSessionBridge::TmuxSessionBridge(Session *gatewaySession, ViewManager *viewManager, QObject *parent)
    : QObject(parent)
    , _gatewaySession(gatewaySession)
    , _viewManager(viewManager)
    , _gateway(new TmuxGateway(gatewaySession, this))
    , _controller(new TmuxController(_gateway, gatewaySession, viewManager, this))
{
    auto *vtEmulation = qobject_cast<Vt102Emulation *>(gatewaySession->emulation());
    if (vtEmulation) {
        connect(vtEmulation, &Vt102Emulation::tmuxControlModeLineReceived, _gateway, &TmuxGateway::processLine);
        connect(vtEmulation, &Vt102Emulation::tmuxControlModeEnded, this, &TmuxSessionBridge::teardown);
    }

    connect(gatewaySession, &Session::finished, this, &TmuxSessionBridge::teardown);

    TmuxControllerRegistry::instance()->registerController(_controller);

    _controller->initialize();
}

TmuxSessionBridge::~TmuxSessionBridge()
{
    TmuxControllerRegistry::instance()->unregisterController(_controller);
}

void TmuxSessionBridge::teardown()
{
    _controller->cleanup();
    this->deleteLater();
}

} // namespace Konsole

#include "moc_TmuxSessionBridge.cpp"
