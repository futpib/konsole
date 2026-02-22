/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXINTEGRATIONTEST_H
#define TMUXINTEGRATIONTEST_H

#include <QObject>

namespace Konsole
{
class TmuxIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testTmuxControlModeExitCleanup();
    void testClosePaneTabThenGatewayTab();
    void testTmuxControlModeAttach();
    void testTmuxTwoPaneSplitAttach();
};

}

#endif // TMUXINTEGRATIONTEST_H
