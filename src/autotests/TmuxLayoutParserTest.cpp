/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxLayoutParserTest.h"

#include <QTest>

#include "../tmux/TmuxLayoutParser.h"

using namespace Konsole;

void TmuxLayoutParserTest::testChecksum()
{
    QString layout = QStringLiteral("b25d,80x24,0,0,0");
    QByteArray body = layout.mid(5).toLatin1(); // "80x24,0,0,0"
    uint16_t expected = layout.left(4).toUShort(nullptr, 16);
    QCOMPARE(TmuxLayoutParser::checksum(body), expected);
}

void TmuxLayoutParserTest::testSerializeSinglePane()
{
    TmuxLayoutNode leaf;
    leaf.type = TmuxLayoutNodeType::Leaf;
    leaf.width = 80;
    leaf.height = 24;
    leaf.xOffset = 0;
    leaf.yOffset = 0;
    leaf.paneId = 0;

    QString result = TmuxLayoutParser::serialize(leaf);
    // Should be "XXXX,80x24,0,0,0" where XXXX is checksum
    QVERIFY(result.endsWith(QStringLiteral(",80x24,0,0,0")));
    // Verify it roundtrips
    auto parsed = TmuxLayoutParser::parse(result);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->type, TmuxLayoutNodeType::Leaf);
    QCOMPARE(parsed->width, 80);
    QCOMPARE(parsed->height, 24);
    QCOMPARE(parsed->paneId, 0);
}

void TmuxLayoutParserTest::testSerializeHSplit()
{
    TmuxLayoutNode root;
    root.type = TmuxLayoutNodeType::HSplit;
    root.width = 81;
    root.height = 24;
    root.xOffset = 0;
    root.yOffset = 0;

    TmuxLayoutNode left;
    left.type = TmuxLayoutNodeType::Leaf;
    left.width = 40;
    left.height = 24;
    left.xOffset = 0;
    left.yOffset = 0;
    left.paneId = 0;

    TmuxLayoutNode right;
    right.type = TmuxLayoutNodeType::Leaf;
    right.width = 40;
    right.height = 24;
    right.xOffset = 41;
    right.yOffset = 0;
    right.paneId = 1;

    root.children = {left, right};

    QString result = TmuxLayoutParser::serialize(root);
    // Body should be "81x24,0,0{40x24,0,0,0,40x24,41,0,1}"
    QString body = result.mid(5);
    QCOMPARE(body, QStringLiteral("81x24,0,0{40x24,0,0,0,40x24,41,0,1}"));

    auto parsed = TmuxLayoutParser::parse(result);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->type, TmuxLayoutNodeType::HSplit);
    QCOMPARE(parsed->children.size(), 2);
    QCOMPARE(parsed->children[0].paneId, 0);
    QCOMPARE(parsed->children[1].paneId, 1);
}

void TmuxLayoutParserTest::testSerializeVSplit()
{
    TmuxLayoutNode root;
    root.type = TmuxLayoutNodeType::VSplit;
    root.width = 80;
    root.height = 49;
    root.xOffset = 0;
    root.yOffset = 0;

    TmuxLayoutNode top;
    top.type = TmuxLayoutNodeType::Leaf;
    top.width = 80;
    top.height = 24;
    top.xOffset = 0;
    top.yOffset = 0;
    top.paneId = 0;

    TmuxLayoutNode bottom;
    bottom.type = TmuxLayoutNodeType::Leaf;
    bottom.width = 80;
    bottom.height = 24;
    bottom.xOffset = 0;
    bottom.yOffset = 25;
    bottom.paneId = 1;

    root.children = {top, bottom};

    QString result = TmuxLayoutParser::serialize(root);
    QString body = result.mid(5);
    QCOMPARE(body, QStringLiteral("80x49,0,0[80x24,0,0,0,80x24,0,25,1]"));

    auto parsed = TmuxLayoutParser::parse(result);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->type, TmuxLayoutNodeType::VSplit);
    QCOMPARE(parsed->children.size(), 2);
}

void TmuxLayoutParserTest::testSerializeNestedSplits()
{
    // {leaf, [leaf, leaf]}
    TmuxLayoutNode root;
    root.type = TmuxLayoutNodeType::HSplit;
    root.width = 81;
    root.height = 49;
    root.xOffset = 0;
    root.yOffset = 0;

    TmuxLayoutNode left;
    left.type = TmuxLayoutNodeType::Leaf;
    left.width = 40;
    left.height = 49;
    left.xOffset = 0;
    left.yOffset = 0;
    left.paneId = 0;

    TmuxLayoutNode rightSplit;
    rightSplit.type = TmuxLayoutNodeType::VSplit;
    rightSplit.width = 40;
    rightSplit.height = 49;
    rightSplit.xOffset = 41;
    rightSplit.yOffset = 0;

    TmuxLayoutNode rightTop;
    rightTop.type = TmuxLayoutNodeType::Leaf;
    rightTop.width = 40;
    rightTop.height = 24;
    rightTop.xOffset = 41;
    rightTop.yOffset = 0;
    rightTop.paneId = 1;

    TmuxLayoutNode rightBottom;
    rightBottom.type = TmuxLayoutNodeType::Leaf;
    rightBottom.width = 40;
    rightBottom.height = 24;
    rightBottom.xOffset = 41;
    rightBottom.yOffset = 25;
    rightBottom.paneId = 2;

    rightSplit.children = {rightTop, rightBottom};
    root.children = {left, rightSplit};

    QString result = TmuxLayoutParser::serialize(root);

    // Roundtrip: parse and re-serialize should produce same body
    auto parsed = TmuxLayoutParser::parse(result);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->type, TmuxLayoutNodeType::HSplit);
    QCOMPARE(parsed->children.size(), 2);
    QCOMPARE(parsed->children[0].type, TmuxLayoutNodeType::Leaf);
    QCOMPARE(parsed->children[1].type, TmuxLayoutNodeType::VSplit);
    QCOMPARE(parsed->children[1].children.size(), 2);

    QString reserialized = TmuxLayoutParser::serialize(parsed.value());
    QCOMPARE(result, reserialized);
}

void TmuxLayoutParserTest::testParseSerializeRoundtrip_data()
{
    QTest::addColumn<QString>("layoutString");

    QTest::newRow("single pane") << QStringLiteral("b25d,80x24,0,0,0");
    QTest::newRow("hsplit 2 pane") << QStringLiteral("00f6,81x24,0,0{40x24,0,0,0,40x24,41,0,1}");
    QTest::newRow("vsplit 2 pane") << QStringLiteral("3d2e,80x49,0,0[80x24,0,0,0,80x24,0,25,1]");
    QTest::newRow("nested h-v") << QStringLiteral("4434,81x49,0,0{40x49,0,0,0,40x49,41,0[40x24,41,0,1,40x24,41,25,2]}");
    QTest::newRow("3-way hsplit") << QStringLiteral("023e,122x24,0,0{40x24,0,0,0,40x24,41,0,1,40x24,82,0,2}");
}

void TmuxLayoutParserTest::testParseSerializeRoundtrip()
{
    QFETCH(QString, layoutString);

    auto parsed = TmuxLayoutParser::parse(layoutString);
    QVERIFY2(parsed.has_value(), qPrintable(QStringLiteral("Failed to parse: ") + layoutString));

    QString reserialized = TmuxLayoutParser::serialize(parsed.value());
    // The body (after checksum) should match
    QCOMPARE(reserialized.mid(5), layoutString.mid(5));
    // The checksum should also match since it's computed from the same body
    QCOMPARE(reserialized, layoutString);
}

void TmuxLayoutParserTest::testSerializeThreeChildSplit()
{
    TmuxLayoutNode root;
    root.type = TmuxLayoutNodeType::HSplit;
    root.width = 122;
    root.height = 24;
    root.xOffset = 0;
    root.yOffset = 0;

    for (int i = 0; i < 3; ++i) {
        TmuxLayoutNode child;
        child.type = TmuxLayoutNodeType::Leaf;
        child.width = 40;
        child.height = 24;
        child.xOffset = i * 41;
        child.yOffset = 0;
        child.paneId = i;
        root.children.append(child);
    }

    QString result = TmuxLayoutParser::serialize(root);
    QString body = result.mid(5);
    QCOMPARE(body, QStringLiteral("122x24,0,0{40x24,0,0,0,40x24,41,0,1,40x24,82,0,2}"));

    // Roundtrip
    auto parsed = TmuxLayoutParser::parse(result);
    QVERIFY(parsed.has_value());
    QString reserialized = TmuxLayoutParser::serialize(parsed.value());
    QCOMPARE(reserialized, result);
}

QTEST_GUILESS_MAIN(TmuxLayoutParserTest)
