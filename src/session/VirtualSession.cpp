/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VirtualSession.h"

#include "Emulation.h"
#include "NullProcessInfo.h"
#include "Pty.h"

namespace Konsole
{

VirtualSession::VirtualSession(QObject *parent)
    : Session(parent)
{
    // Disconnect all PTY-related signals and delete the PTY.
    // We use blanket disconnects since the specific signal→slot pairs
    // involve protected members that can't be referenced by pointer-to-member.
    if (_shellProcess) {
        // All connections between _shellProcess ↔ this
        disconnect(_shellProcess, nullptr, this, nullptr);
        // All connections between _emulation ↔ _shellProcess
        disconnect(_emulation, nullptr, _shellProcess, nullptr);
        disconnect(_shellProcess, nullptr, _emulation, nullptr);

        delete _shellProcess;
        _shellProcess = nullptr;
    }

    // Disconnect imageSizeInitialized → run (queued connection set up in openTeletype)
    // and imageSizeChanged → updateWindowSize (also set up in openTeletype).
    // These are emulation→this connections that reference the PTY indirectly.
    disconnect(_emulation, &Konsole::Emulation::imageSizeInitialized, this, nullptr);

    // Provide a NullProcessInfo so getProcessInfo() never returns null
    delete _sessionProcessInfo;
    _sessionProcessInfo = new NullProcessInfo(0);
}

void VirtualSession::injectData(const char *data, int length)
{
    if (_emulation) {
        _emulation->receiveData(data, length);
    }
}

bool VirtualSession::isVirtual() const
{
    return true;
}

void VirtualSession::run()
{
    // No-op: virtual sessions have no PTY to start
}

void VirtualSession::close()
{
    Q_EMIT finished(this);
}

} // namespace Konsole

#include "moc_VirtualSession.cpp"
