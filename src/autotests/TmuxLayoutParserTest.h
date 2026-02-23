/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXLAYOUTPARSERTEST_H
#define TMUXLAYOUTPARSERTEST_H

#include <QObject>

namespace Konsole
{
class TmuxLayoutParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testChecksum();
    void testSerializeSinglePane();
    void testSerializeHSplit();
    void testSerializeVSplit();
    void testSerializeNestedSplits();
    void testParseSerializeRoundtrip();
    void testParseSerializeRoundtrip_data();
    void testSerializeThreeChildSplit();
};
}

#endif // TMUXLAYOUTPARSERTEST_H
