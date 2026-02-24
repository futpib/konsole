/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxTestDSLTest.h"
#include "TmuxTestDSL.h"

#include <QTest>

using namespace Konsole;
using namespace Konsole::TmuxTestDSL;

void TmuxTestDSLTest::testParseSinglePane()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┐
        │ id: A    │
        │ cmd:     │
        │ sleep 30 │
        └──────────┘
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.pane.id, QStringLiteral("A"));
    QCOMPARE(spec.layout.pane.cmd, QStringLiteral("sleep 30"));
}

void TmuxTestDSLTest::testParseTwoHorizontalPanes()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┬──────────┐
        │ id: L    │ id: R    │
        │ cmd:     │ cmd:     │
        │ sleep 30 │ sleep 30 │
        └──────────┴──────────┘
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::HSplit);
    QCOMPARE(spec.layout.children.size(), 2);

    QCOMPARE(spec.layout.children[0].type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.children[0].pane.id, QStringLiteral("L"));
    QCOMPARE(spec.layout.children[0].pane.cmd, QStringLiteral("sleep 30"));

    QCOMPARE(spec.layout.children[1].type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.children[1].pane.id, QStringLiteral("R"));
    QCOMPARE(spec.layout.children[1].pane.cmd, QStringLiteral("sleep 30"));
}

void TmuxTestDSLTest::testParseTwoVerticalPanes()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┐
        │ id: T    │
        │ cmd:     │
        │ sleep 30 │
        ├──────────┤
        │ id: B    │
        │ cmd:     │
        │ sleep 30 │
        └──────────┘
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::VSplit);
    QCOMPARE(spec.layout.children.size(), 2);

    QCOMPARE(spec.layout.children[0].type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.children[0].pane.id, QStringLiteral("T"));

    QCOMPARE(spec.layout.children[1].type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.children[1].pane.id, QStringLiteral("B"));
}

void TmuxTestDSLTest::testParseNestedLayout()
{
    // [ L | [ RT / RB ] ]
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┬──────────┐
        │ id: L    │ id: RT   │
        │ cmd:     │ cmd:     │
        │ sleep 60 │ sleep 60 │
        │          ├──────────┤
        │          │ id: RB   │
        │          │ cmd:     │
        │          │ sleep 60 │
        └──────────┴──────────┘
        size: 160x40
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::HSplit);
    QCOMPARE(spec.layout.children.size(), 2);

    // Left child is a leaf
    QCOMPARE(spec.layout.children[0].type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.children[0].pane.id, QStringLiteral("L"));

    // Right child is a VSplit
    QCOMPARE(spec.layout.children[1].type, LayoutSpec::VSplit);
    QCOMPARE(spec.layout.children[1].children.size(), 2);
    QCOMPARE(spec.layout.children[1].children[0].pane.id, QStringLiteral("RT"));
    QCOMPARE(spec.layout.children[1].children[1].pane.id, QStringLiteral("RB"));

    // Footer
    QVERIFY(spec.size.has_value());
    QCOMPARE(spec.size->first, 160);
    QCOMPARE(spec.size->second, 40);
}

void TmuxTestDSLTest::testParseFooterMetadata()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┬──────────┐
        │          │          │
        └──────────┴──────────┘
        size: 180x40
        panes: 2
        orientation: horizontal
        tab: bash
        ratio: 3:1
    )"));

    QVERIFY(spec.size.has_value());
    QCOMPARE(spec.size->first, 180);
    QCOMPARE(spec.size->second, 40);

    QVERIFY(spec.paneCount.has_value());
    QCOMPARE(spec.paneCount.value(), 2);

    QVERIFY(spec.orientation.has_value());
    QCOMPARE(spec.orientation.value(), QStringLiteral("horizontal"));

    QVERIFY(spec.tab.has_value());
    QCOMPARE(spec.tab.value(), QStringLiteral("bash"));

    QVERIFY(spec.ratio.has_value());
    QCOMPARE(spec.ratio->size(), 2);
    QCOMPARE(spec.ratio->at(0), 3);
    QCOMPARE(spec.ratio->at(1), 1);
}

void TmuxTestDSLTest::testParsePaneAnnotations()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────────────┐
        │ id: main         │
        │ cmd: sleep 30    │
        │ contains: MARKER │
        │ focused: true    │
        │ columns: 80      │
        │ lines: 24        │
        │ title: bash      │
        └──────────────────┘
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::Leaf);
    auto &pane = spec.layout.pane;
    QCOMPARE(pane.id, QStringLiteral("main"));
    QCOMPARE(pane.cmd, QStringLiteral("sleep 30"));
    QCOMPARE(pane.contains.size(), 1);
    QCOMPARE(pane.contains[0], QStringLiteral("MARKER"));
    QVERIFY(pane.focused.has_value());
    QCOMPARE(pane.focused.value(), true);
    QVERIFY(pane.columns.has_value());
    QCOMPARE(pane.columns.value(), 80);
    QVERIFY(pane.lines.has_value());
    QCOMPARE(pane.lines.value(), 24);
    QCOMPARE(pane.title, QStringLiteral("bash"));
}

void TmuxTestDSLTest::testParseMultilineCommand()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┐
        │ id: A    │
        │ cmd:     │
        │ sleep 30 │
        └──────────┘
    )"));

    QCOMPARE(spec.layout.pane.cmd, QStringLiteral("sleep 30"));
}

void TmuxTestDSLTest::testParseFourPaneGrid()
{
    // [ [ TL / BL ] | [ TR / BR ] ]
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┬──────────┐
        │ id: TL   │ id: TR   │
        ├──────────┼──────────┤
        │ id: BL   │ id: BR   │
        └──────────┴──────────┘
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::HSplit);
    QCOMPARE(spec.layout.children.size(), 2);

    // Left column is VSplit
    QCOMPARE(spec.layout.children[0].type, LayoutSpec::VSplit);
    QCOMPARE(spec.layout.children[0].children.size(), 2);
    QCOMPARE(spec.layout.children[0].children[0].pane.id, QStringLiteral("TL"));
    QCOMPARE(spec.layout.children[0].children[1].pane.id, QStringLiteral("BL"));

    // Right column is VSplit
    QCOMPARE(spec.layout.children[1].type, LayoutSpec::VSplit);
    QCOMPARE(spec.layout.children[1].children.size(), 2);
    QCOMPARE(spec.layout.children[1].children[0].pane.id, QStringLiteral("TR"));
    QCOMPARE(spec.layout.children[1].children[1].pane.id, QStringLiteral("BR"));
}

void TmuxTestDSLTest::testParseThreeHorizontalPanes()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────┬──────┬──────┐
        │ id:A │ id:B │ id:C │
        └──────┴──────┴──────┘
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::HSplit);
    QCOMPARE(spec.layout.children.size(), 3);
    QCOMPARE(spec.layout.children[0].pane.id, QStringLiteral("A"));
    QCOMPARE(spec.layout.children[1].pane.id, QStringLiteral("B"));
    QCOMPARE(spec.layout.children[2].pane.id, QStringLiteral("C"));
}

void TmuxTestDSLTest::testParseEmptyPanes()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┬──────────┐
        │          │          │
        └──────────┴──────────┘
        orientation: horizontal
        panes: 2
    )"));

    QCOMPARE(spec.layout.type, LayoutSpec::HSplit);
    QCOMPARE(spec.layout.children.size(), 2);
    QCOMPARE(spec.layout.children[0].type, LayoutSpec::Leaf);
    QCOMPARE(spec.layout.children[1].type, LayoutSpec::Leaf);
    // Panes should have empty annotations
    QVERIFY(spec.layout.children[0].pane.id.isEmpty());
    QVERIFY(spec.layout.children[1].pane.id.isEmpty());
}

void TmuxTestDSLTest::testCountPanes()
{
    auto spec = parse(QStringLiteral(R"(
        ┌──────────┬──────────┐
        │ id: L    │ id: RT   │
        │          ├──────────┤
        │          │ id: RB   │
        └──────────┴──────────┘
    )"));

    QCOMPARE(countPanes(spec.layout), 3);
}

QTEST_MAIN(TmuxTestDSLTest)

#include "moc_TmuxTestDSLTest.cpp"
