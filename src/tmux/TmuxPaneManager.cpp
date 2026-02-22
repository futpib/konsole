/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxPaneManager.h"

#include "TmuxGateway.h"

#include "Emulation.h"
#include "profile/ProfileManager.h"
#include "session/SessionManager.h"
#include "session/VirtualSession.h"

namespace Konsole
{

TmuxPaneManager::TmuxPaneManager(TmuxGateway *gateway, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
{
}

Session *TmuxPaneManager::createPaneSession(int paneId)
{
    if (_paneToSession.contains(paneId)) {
        return _paneToSession[paneId];
    }

    VirtualSession *session = SessionManager::instance()->createVirtualSession(
        ProfileManager::instance()->defaultProfile());
    session->setPaneSyncPolicy(Session::PaneSyncPolicy::SyncWithSiblings);

    connect(session->emulation(), &Emulation::sendData, this, [this, paneId](const QByteArray &data) {
        _gateway->sendKeys(paneId, data);
    });

    connect(session->emulation(), &Emulation::imageSizeChanged, this, [this](int, int) {
        Q_EMIT paneViewSizeChanged();
    });

    connect(session, &QObject::destroyed, this, [this, paneId]() {
        _paneToSession.remove(paneId);
    });

    _paneToSession[paneId] = session;
    return session;
}

void TmuxPaneManager::destroyPaneSession(int paneId)
{
    auto it = _paneToSession.find(paneId);
    if (it != _paneToSession.end()) {
        Session *session = it.value();
        _paneToSession.erase(it);
        session->close();
    }
}

void TmuxPaneManager::destroyAllPaneSessions()
{
    const auto paneIds = _paneToSession.keys();
    for (int paneId : paneIds) {
        destroyPaneSession(paneId);
    }
}

void TmuxPaneManager::deliverOutput(int paneId, const QByteArray &data)
{
    if (_pausedPanes.contains(paneId)) {
        _pauseBuffers[paneId].append(data);
        return;
    }

    auto *session = qobject_cast<VirtualSession *>(_paneToSession.value(paneId, nullptr));
    if (session) {
        session->injectData(data.constData(), data.size());
    }
}

void TmuxPaneManager::pausePane(int paneId)
{
    _pausedPanes.insert(paneId);
    _gateway->sendCommand(QStringLiteral("refresh-client -A '%") + QString::number(paneId) + QStringLiteral(":on'"));
}

void TmuxPaneManager::continuePane(int paneId)
{
    _pausedPanes.remove(paneId);

    if (_pauseBuffers.contains(paneId)) {
        QByteArray buffered = _pauseBuffers.take(paneId);
        if (!buffered.isEmpty()) {
            auto *session = qobject_cast<VirtualSession *>(_paneToSession.value(paneId, nullptr));
            if (session) {
                session->injectData(buffered.constData(), buffered.size());
            }
        }
    }
}

bool TmuxPaneManager::hasPane(int paneId) const
{
    return _paneToSession.contains(paneId);
}

int TmuxPaneManager::paneIdForSession(Session *session) const
{
    for (auto it = _paneToSession.constBegin(); it != _paneToSession.constEnd(); ++it) {
        if (it.value() == session) {
            return it.key();
        }
    }
    return -1;
}

Session *TmuxPaneManager::sessionForPane(int paneId) const
{
    return _paneToSession.value(paneId, nullptr);
}

const QMap<int, Session *> &TmuxPaneManager::paneToSession() const
{
    return _paneToSession;
}

} // namespace Konsole

#include "moc_TmuxPaneManager.cpp"
