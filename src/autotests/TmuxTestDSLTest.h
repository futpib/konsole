/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXTESTDSLTEST_H
#define TMUXTESTDSLTEST_H

#include <QObject>

namespace Konsole
{
class TmuxTestDSLTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testParseSinglePane();
    void testParseTwoHorizontalPanes();
    void testParseTwoVerticalPanes();
    void testParseNestedLayout();
    void testParseFooterMetadata();
    void testParsePaneAnnotations();
    void testParseMultilineCommand();
    void testParseFourPaneGrid();
    void testParseThreeHorizontalPanes();
    void testParseEmptyPanes();
    void testCountPanes();
};

}

#endif // TMUXTESTDSLTEST_H
