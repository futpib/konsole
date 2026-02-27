/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SCREENWINDOWTEST_H
#define SCREENWINDOWTEST_H

#include <QObject>

namespace Konsole
{

class ScreenWindowTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testScrollToWithSmallLineCount();
    void testAtEndOfOutputWithSmallLineCount();
};

}

#endif // SCREENWINDOWTEST_H
