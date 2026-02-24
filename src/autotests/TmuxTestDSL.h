/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXTESTDSL_H
#define TMUXTESTDSL_H

#include <QList>
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

namespace Konsole
{

class MainWindow;
class Session;
class TabbedViewContainer;
class ViewManager;

namespace TmuxTestDSL
{

struct PaneSpec {
    QString id;
    QString cmd;
    QString title;
    QStringList contains;
    std::optional<bool> focused;
    std::optional<int> columns;
    std::optional<int> lines;
};

struct LayoutSpec {
    enum Type { Leaf, HSplit, VSplit };
    Type type = Leaf;
    PaneSpec pane; // Leaf only
    QList<LayoutSpec> children; // Split only
};

struct DiagramSpec {
    LayoutSpec layout;
    std::optional<QPair<int, int>> size; // cols x rows
    std::optional<int> paneCount;
    std::optional<QString> orientation;
    std::optional<QString> tab;
    std::optional<QList<int>> ratio;
};

struct SessionContext {
    QString sessionName;
    QMap<QString, int> idToPaneId;
};

struct AttachResult {
    QPointer<MainWindow> mw;
    Session *gatewaySession = nullptr;
    QPointer<TabbedViewContainer> container;
};

// Parse a box-drawing diagram string into a DiagramSpec
DiagramSpec parse(const QString &diagram);

// Create a detached tmux session matching the diagram, then verify it was created correctly.
// Populates ctx with session name and pane ID mapping.
void setupTmuxSession(const DiagramSpec &spec, const QString &tmuxPath, SessionContext &ctx);

// Attach Konsole to an existing tmux session via -CC control mode.
// Waits for virtual pane tabs to appear.
void attachKonsole(const QString &tmuxPath, const QString &sessionName, AttachResult &result);

// Assert that the Konsole ViewSplitter tree matches the diagram structure.
void assertKonsoleLayout(const DiagramSpec &spec, ViewManager *vm, Session *gatewaySession);

// Assert that tmux pane state matches the diagram.
void assertTmuxLayout(const DiagramSpec &spec, const QString &tmuxPath, const QString &sessionName);

// Kill a tmux session.
void killTmuxSession(const QString &tmuxPath, const QString &sessionName);

// Find tmux executable or call QSKIP. Returns the path.
QString findTmuxOrSkip();

// Count total leaf panes in a LayoutSpec tree.
int countPanes(const LayoutSpec &layout);

} // namespace TmuxTestDSL

} // namespace Konsole

#endif // TMUXTESTDSL_H
