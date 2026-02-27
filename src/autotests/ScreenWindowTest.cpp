/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ScreenWindowTest.h"

#include <QTest>

#include "../Screen.h"
#include "../ScreenWindow.h"

using namespace Konsole;

void ScreenWindowTest::testScrollToWithSmallLineCount()
{
    // Regression test: scrollTo() used to crash when lineCount() < windowLines()
    // because qBound(0, line, maxCurrentLineNumber) fails when max < min.
    // This happens with tmux virtual sessions where the screen may not have
    // been sized before search bar triggers scrollTo().

    Screen screen(5, 80);
    ScreenWindow window(&screen);
    window.setWindowLines(40);

    // lineCount() = 5, windowLines() = 40, so lineCount() - windowLines() = -35
    QVERIFY(window.lineCount() < window.windowLines());

    // This must not crash
    window.scrollTo(0);
    QCOMPARE(window.currentLine(), 0);

    window.scrollTo(10);
    QCOMPARE(window.currentLine(), 0);
}

void ScreenWindowTest::testAtEndOfOutputWithSmallLineCount()
{
    Screen screen(5, 80);
    ScreenWindow window(&screen);
    window.setWindowLines(40);

    QVERIFY(window.lineCount() < window.windowLines());

    // This must not crash and should return true (we're at line 0,
    // which is the max possible line when lineCount < windowLines)
    QVERIFY(window.atEndOfOutput());
}

QTEST_GUILESS_MAIN(ScreenWindowTest)

#include "moc_ScreenWindowTest.cpp"
