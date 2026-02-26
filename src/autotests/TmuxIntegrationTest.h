/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXINTEGRATIONTEST_H
#define TMUXINTEGRATIONTEST_H

#include <QObject>
#include <QTemporaryDir>

namespace Konsole
{
class TmuxIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void testTmuxControlModeExitCleanup();
    void testClosePaneTabThenGatewayTab();
    void testTmuxControlModeAttach();
    void testTmuxTwoPaneSplitAttach();
    void testTmuxAttachContentRecovery();
    void testTmuxAttachComplexPromptRecovery();
    void testSplitterResizePropagatedToTmux();
    void testTmuxPaneTitleInfo();
    void testSplitPaneFocusesNewPane();
    void testSplitPaneFocusesNewPaneComplexLayout();
    void testSplitPaneFocusesNewPaneNestedLayout();
    void testResizePropagatedToPty();
    void testForcedSizeFromSmallerClient();
    void testForcedSizeFromSmallerClientMultiPane();

private:
    QTemporaryDir m_tmuxTmpDir;
};

}

#endif // TMUXINTEGRATIONTEST_H
