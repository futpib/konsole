/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VirtualSession.h"

#include "Emulation.h"

namespace Konsole
{

VirtualSession::VirtualSession(QObject *parent)
    : Session(NoPtyTag{}, parent)
{
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
