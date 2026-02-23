/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXPANESTATERECOVERYTEST_H
#define TMUXPANESTATERECOVERYTEST_H

#include <QObject>

namespace Konsole
{
class TmuxPaneStateRecoveryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCapturePaneContentRecovery();
    void testCapturePaneWithEscapeSequences();
    void testCapturePaneRealisticPrompt();
    void testCapturePaneWideMismatch();
    void testCapturePaneFromRealTmux();
};
}

#endif // TMUXPANESTATERECOVERYTEST_H
