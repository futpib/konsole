/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxIntegrationTest.h"

#include <QPointer>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "../Emulation.h"
#include "../MainWindow.h"
#include "../Screen.h"
#include "../ScreenWindow.h"
#include "../ViewManager.h"
#include "../profile/ProfileManager.h"
#include "../session/Session.h"
#include "../session/SessionManager.h"
#include "../terminalDisplay/TerminalDisplay.h"
#include "../widgets/ViewContainer.h"
#include "../widgets/ViewSplitter.h"

using namespace Konsole;

void TmuxIntegrationTest::testTmuxControlModeExitCleanup()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    if (tmuxPath.isEmpty()) {
        QSKIP("tmux command not found.");
    }

    // Simulate: konsole -e 'tmux -CC new-session "sleep 1 && exit 0"'
    auto *mw = new MainWindow();
    QPointer<MainWindow> mwGuard(mw);
    ViewManager *vm = mw->viewManager();

    // Set up profile with custom command, like -e does
    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, tmuxPath);
    profile->setProperty(Profile::Arguments, QStringList{tmuxPath, QStringLiteral("-CC"), QStringLiteral("new-session"), QStringLiteral("sleep 1 && exit 0")});

    Session *session = vm->createSession(profile, QString());
    QPointer<Session> sessionGuard(session);
    auto *view = vm->createView(session);
    vm->activeContainer()->addView(view);
    session->run();

    QPointer<TabbedViewContainer> container = vm->activeContainer();
    QVERIFY(container);
    QCOMPARE(container->count(), 1);

    // Wait for tmux control mode to create virtual pane tab(s)
    QTRY_VERIFY_WITH_TIMEOUT(container && container->count() >= 2, 10000);

    // Wait for tmux to exit — the window may close and delete itself
    QTRY_VERIFY_WITH_TIMEOUT(!mwGuard || !container || container->count() <= 1, 15000);

    // If the window is still alive, clean up
    delete mwGuard.data();
}

void TmuxIntegrationTest::testClosePaneTabThenGatewayTab()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    if (tmuxPath.isEmpty()) {
        QSKIP("tmux command not found.");
    }

    // Simulate: konsole -e 'tmux -CC new-session "sleep 30"'
    auto *mw = new MainWindow();
    QPointer<MainWindow> mwGuard(mw);
    ViewManager *vm = mw->viewManager();

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, tmuxPath);
    profile->setProperty(Profile::Arguments, QStringList{tmuxPath, QStringLiteral("-CC"), QStringLiteral("new-session"), QStringLiteral("sleep 30")});

    Session *gatewaySession = vm->createSession(profile, QString());
    auto *view = vm->createView(gatewaySession);
    vm->activeContainer()->addView(view);
    gatewaySession->run();

    QPointer<TabbedViewContainer> container = vm->activeContainer();
    QVERIFY(container);
    QCOMPARE(container->count(), 1);

    // Wait for tmux control mode to create virtual pane tab(s)
    QTRY_VERIFY_WITH_TIMEOUT(container && container->count() >= 2, 10000);

    // Find the pane session (the one that isn't the gateway)
    Session *paneSession = nullptr;
    const auto sessions = vm->sessions();
    for (Session *s : sessions) {
        if (s != gatewaySession) {
            paneSession = s;
            break;
        }
    }
    QVERIFY(paneSession);

    // Close the pane tab (like clicking the tab close icon)
    paneSession->closeInNormalWay();

    // Close the gateway tab (like pressing Ctrl+W)
    gatewaySession->closeInNormalWay();

    // Wait for everything to tear down — the window may close and delete itself
    QTRY_VERIFY_WITH_TIMEOUT(!mwGuard, 10000);

    delete mwGuard.data();
}

void TmuxIntegrationTest::testTmuxControlModeAttach()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    if (tmuxPath.isEmpty()) {
        QSKIP("tmux command not found.");
    }

    // Create a detached tmux session with a unique name
    const QString sessionName = QStringLiteral("konsole-test-%1").arg(QCoreApplication::applicationPid());

    QProcess tmuxNewSession;
    tmuxNewSession.start(tmuxPath, {QStringLiteral("new-session"), QStringLiteral("-d"), QStringLiteral("-s"), sessionName, QStringLiteral("sleep 30")});
    QVERIFY(tmuxNewSession.waitForFinished(5000));
    QCOMPARE(tmuxNewSession.exitCode(), 0);

    // Simulate: konsole -e 'tmux -CC attach -t <sessionName>'
    auto *mw = new MainWindow();
    QPointer<MainWindow> mwGuard(mw);
    ViewManager *vm = mw->viewManager();

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, tmuxPath);
    profile->setProperty(Profile::Arguments, QStringList{tmuxPath, QStringLiteral("-CC"), QStringLiteral("attach"), QStringLiteral("-t"), sessionName});

    Session *gatewaySession = vm->createSession(profile, QString());
    auto *view = vm->createView(gatewaySession);
    vm->activeContainer()->addView(view);
    gatewaySession->run();

    QPointer<TabbedViewContainer> container = vm->activeContainer();
    QVERIFY(container);
    QCOMPARE(container->count(), 1);

    // Wait for tmux control mode to create virtual pane tab(s)
    QTRY_VERIFY_WITH_TIMEOUT(container && container->count() >= 2, 10000);

    // Close the pane tab, then the gateway tab
    Session *paneSession = nullptr;
    const auto sessions = vm->sessions();
    for (Session *s : sessions) {
        if (s != gatewaySession) {
            paneSession = s;
            break;
        }
    }
    QVERIFY(paneSession);

    paneSession->closeInNormalWay();
    gatewaySession->closeInNormalWay();

    // Wait for everything to tear down
    QTRY_VERIFY_WITH_TIMEOUT(!mwGuard, 10000);

    delete mwGuard.data();

    // Clean up the tmux session
    QProcess tmuxKill;
    tmuxKill.start(tmuxPath, {QStringLiteral("kill-session"), QStringLiteral("-t"), sessionName});
    tmuxKill.waitForFinished(5000);
}

void TmuxIntegrationTest::testTmuxTwoPaneSplitAttach()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    if (tmuxPath.isEmpty()) {
        QSKIP("tmux command not found.");
    }

    // Create a detached tmux session with two panes (horizontal split)
    const QString sessionName = QStringLiteral("konsole-split-test-%1").arg(QCoreApplication::applicationPid());

    QProcess tmuxNewSession;
    tmuxNewSession.start(tmuxPath, {QStringLiteral("new-session"), QStringLiteral("-d"), QStringLiteral("-s"), sessionName, QStringLiteral("-x"), QStringLiteral("180"), QStringLiteral("-y"), QStringLiteral("40"), QStringLiteral("sleep 30")});
    QVERIFY(tmuxNewSession.waitForFinished(5000));
    QCOMPARE(tmuxNewSession.exitCode(), 0);

    // Split the window horizontally to create a second pane
    QProcess tmuxSplit;
    tmuxSplit.start(tmuxPath, {QStringLiteral("split-window"), QStringLiteral("-h"), QStringLiteral("-t"), sessionName, QStringLiteral("sleep 30")});
    QVERIFY(tmuxSplit.waitForFinished(5000));
    QCOMPARE(tmuxSplit.exitCode(), 0);

    // Attach Konsole to this two-pane session via control mode
    auto *mw = new MainWindow();
    QPointer<MainWindow> mwGuard(mw);
    ViewManager *vm = mw->viewManager();

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, tmuxPath);
    profile->setProperty(Profile::Arguments, QStringList{tmuxPath, QStringLiteral("-CC"), QStringLiteral("attach"), QStringLiteral("-t"), sessionName});

    Session *gatewaySession = vm->createSession(profile, QString());
    auto *view = vm->createView(gatewaySession);
    vm->activeContainer()->addView(view);
    gatewaySession->run();

    QPointer<TabbedViewContainer> container = vm->activeContainer();
    QVERIFY(container);
    QCOMPARE(container->count(), 1);

    // Wait for tmux control mode to create virtual pane tab(s)
    QTRY_VERIFY_WITH_TIMEOUT(container && container->count() >= 2, 10000);

    // Find the tab that contains the split panes (not the gateway tab)
    // The tmux pane tab should be a ViewSplitter with 2 TerminalDisplay children
    ViewSplitter *paneSplitter = nullptr;
    for (int i = 0; i < container->count(); ++i) {
        auto *splitter = qobject_cast<ViewSplitter *>(container->widget(i));
        if (splitter) {
            auto terminals = splitter->findChildren<TerminalDisplay *>();
            qDebug() << "Tab" << i << "has" << terminals.size() << "TerminalDisplay children,"
                     << splitter->count() << "direct children,"
                     << "orientation:" << splitter->orientation();
            if (terminals.size() == 2) {
                paneSplitter = splitter;
                break;
            }
        }
    }
    QVERIFY2(paneSplitter, "Expected a ViewSplitter with 2 TerminalDisplay children for the two-pane tmux layout");

    // Verify the splitter orientation is horizontal (matching the -h split)
    QCOMPARE(paneSplitter->orientation(), Qt::Horizontal);

    // Clean up: close pane sessions, then gateway
    const auto sessions = vm->sessions();
    for (Session *s : sessions) {
        if (s != gatewaySession) {
            s->closeInNormalWay();
        }
    }
    gatewaySession->closeInNormalWay();

    QTRY_VERIFY_WITH_TIMEOUT(!mwGuard, 10000);
    delete mwGuard.data();

    // Kill the tmux session
    QProcess tmuxKill;
    tmuxKill.start(tmuxPath, {QStringLiteral("kill-session"), QStringLiteral("-t"), sessionName});
    tmuxKill.waitForFinished(5000);
}

// Helper: read all visible text from a Session's screen
static QString readSessionScreenText(Session *session)
{
    ScreenWindow *window = session->emulation()->createWindow();
    Screen *screen = window->screen();

    int lines = screen->getLines();
    int columns = screen->getColumns();

    screen->setSelectionStart(0, 0, false);
    screen->setSelectionEnd(columns, lines - 1, false);
    return screen->selectedText(Screen::PlainText);
    // Don't delete window — Emulation::~Emulation owns it via _windows list
}

void TmuxIntegrationTest::testTmuxAttachContentRecovery()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    if (tmuxPath.isEmpty()) {
        QSKIP("tmux command not found.");
    }

    const QString bashPath = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (bashPath.isEmpty()) {
        QSKIP("bash command not found.");
    }

    // Create a detached tmux session running bash with a known size
    const QString sessionName = QStringLiteral("konsole-content-test-%1").arg(QCoreApplication::applicationPid());

    QProcess tmuxNewSession;
    tmuxNewSession.start(tmuxPath,
                         {QStringLiteral("new-session"), QStringLiteral("-d"), QStringLiteral("-s"), sessionName,
                          QStringLiteral("-x"), QStringLiteral("80"), QStringLiteral("-y"), QStringLiteral("24"),
                          bashPath, QStringLiteral("--norc"), QStringLiteral("--noprofile")});
    QVERIFY(tmuxNewSession.waitForFinished(5000));
    QCOMPARE(tmuxNewSession.exitCode(), 0);

    // Send a command with Unicode output
    QProcess sendKeys;
    sendKeys.start(tmuxPath, {QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName,
                              QStringLiteral("echo 'MARKER_START ★ Unicode → Test ✓ MARKER_END'"), QStringLiteral("Enter")});
    QVERIFY(sendKeys.waitForFinished(5000));
    QCOMPARE(sendKeys.exitCode(), 0);

    // Wait for the command to execute
    QTest::qWait(500);

    // Now attach Konsole via -CC
    auto *mw = new MainWindow();
    QPointer<MainWindow> mwGuard(mw);
    ViewManager *vm = mw->viewManager();

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, tmuxPath);
    profile->setProperty(Profile::Arguments, QStringList{tmuxPath, QStringLiteral("-CC"), QStringLiteral("attach"), QStringLiteral("-t"), sessionName});

    Session *gatewaySession = vm->createSession(profile, QString());
    auto *view = vm->createView(gatewaySession);
    vm->activeContainer()->addView(view);
    gatewaySession->run();

    QPointer<TabbedViewContainer> container = vm->activeContainer();
    QVERIFY(container);
    QCOMPARE(container->count(), 1);

    // Wait for tmux control mode to create virtual pane tab(s)
    QTRY_VERIFY_WITH_TIMEOUT(container && container->count() >= 2, 10000);

    // Find the pane session (not the gateway)
    Session *paneSession = nullptr;
    const auto sessions = vm->sessions();
    for (Session *s : sessions) {
        if (s != gatewaySession) {
            paneSession = s;
            break;
        }
    }
    QVERIFY(paneSession);

    // Wait a bit for capture-pane history to be injected
    QTest::qWait(2000);

    // Read the screen content
    QString screenText = readSessionScreenText(paneSession);
    qDebug() << "Pane screen text:" << screenText;

    QVERIFY2(screenText.contains(QStringLiteral("MARKER_START")),
             qPrintable(QStringLiteral("Pane screen should contain 'MARKER_START', got: ") + screenText));
    QVERIFY2(screenText.contains(QStringLiteral("MARKER_END")),
             qPrintable(QStringLiteral("Pane screen should contain 'MARKER_END', got: ") + screenText));

    // Cleanup: close pane sessions first, then gateway
    const auto allSessions = vm->sessions();
    for (Session *s : allSessions) {
        if (s != gatewaySession) {
            s->closeInNormalWay();
        }
    }
    gatewaySession->closeInNormalWay();
    QTRY_VERIFY_WITH_TIMEOUT(!mwGuard, 10000);
    delete mwGuard.data();

    QProcess tmuxKill;
    tmuxKill.start(tmuxPath, {QStringLiteral("kill-session"), QStringLiteral("-t"), sessionName});
    tmuxKill.waitForFinished(5000);
}

void TmuxIntegrationTest::testTmuxAttachComplexPromptRecovery()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    if (tmuxPath.isEmpty()) {
        QSKIP("tmux command not found.");
    }

    const QString bashPath = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (bashPath.isEmpty()) {
        QSKIP("bash command not found.");
    }

    // Create a detached tmux session running bash with a complex PS1 prompt
    // similar to the user's real prompt with Unicode and ANSI escape codes
    const QString sessionName = QStringLiteral("konsole-prompt-test-%1").arg(QCoreApplication::applicationPid());

    QProcess tmuxNewSession;
    tmuxNewSession.start(tmuxPath,
                         {QStringLiteral("new-session"), QStringLiteral("-d"), QStringLiteral("-s"), sessionName,
                          QStringLiteral("-x"), QStringLiteral("240"), QStringLiteral("-y"), QStringLiteral("24"),
                          bashPath, QStringLiteral("--norc"), QStringLiteral("--noprofile")});
    QVERIFY(tmuxNewSession.waitForFinished(5000));
    QCOMPARE(tmuxNewSession.exitCode(), 0);

    // Set a complex PS1 prompt with ANSI colors and Unicode, similar to the user's:
    // [15:25:11] [user@host ~/code/project] ────...──── bash 5.3.9(1)-release  →
    // The long ──── line fills most of the 240-column width
    QProcess sendPS1;
    sendPS1.start(tmuxPath, {QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName,
                             QStringLiteral("PS1='\\[\\033[36m\\][\\t] [\\u@\\h \\w] \\[\\033[33m\\]────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────── \\[\\033[35m\\]\\s \\V  \\[\\033[32m\\]→ \\[\\033[0m\\]'"),
                             QStringLiteral("Enter")});
    QVERIFY(sendPS1.waitForFinished(5000));
    QCOMPARE(sendPS1.exitCode(), 0);

    // Wait for prompt to render
    QTest::qWait(500);

    // Run a command so we have some content
    QProcess sendCmd;
    sendCmd.start(tmuxPath, {QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName,
                             QStringLiteral("echo 'PROMPT_TEST_OUTPUT'"), QStringLiteral("Enter")});
    QVERIFY(sendCmd.waitForFinished(5000));
    QCOMPARE(sendCmd.exitCode(), 0);

    // Wait for command to execute
    QTest::qWait(500);

    // Now attach Konsole via -CC
    auto *mw = new MainWindow();
    QPointer<MainWindow> mwGuard(mw);
    ViewManager *vm = mw->viewManager();

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, tmuxPath);
    profile->setProperty(Profile::Arguments, QStringList{tmuxPath, QStringLiteral("-CC"), QStringLiteral("attach"), QStringLiteral("-t"), sessionName});

    Session *gatewaySession = vm->createSession(profile, QString());
    auto *view = vm->createView(gatewaySession);
    vm->activeContainer()->addView(view);
    gatewaySession->run();

    QPointer<TabbedViewContainer> container = vm->activeContainer();
    QVERIFY(container);
    QCOMPARE(container->count(), 1);

    // Wait for tmux control mode to create virtual pane tab(s)
    QTRY_VERIFY_WITH_TIMEOUT(container && container->count() >= 2, 10000);

    // Find the pane session (not the gateway)
    Session *paneSession = nullptr;
    const auto sessions = vm->sessions();
    for (Session *s : sessions) {
        if (s != gatewaySession) {
            paneSession = s;
            break;
        }
    }
    QVERIFY(paneSession);

    // Wait for capture-pane history to be injected
    QTest::qWait(2000);

    // Read the screen content
    QString screenText = readSessionScreenText(paneSession);
    qDebug() << "Complex prompt pane screen text:" << screenText;

    // The capture-pane without -e strips ANSI codes, so we should see the
    // plain text content of the prompt and command output
    QVERIFY2(screenText.contains(QStringLiteral("PROMPT_TEST_OUTPUT")),
             qPrintable(QStringLiteral("Pane screen should contain 'PROMPT_TEST_OUTPUT', got: ") + screenText));

    // Verify we see the Unicode arrow from the prompt (capture-pane preserves Unicode text)
    QVERIFY2(screenText.contains(QStringLiteral("→")),
             qPrintable(QStringLiteral("Pane screen should contain '→' from prompt, got: ") + screenText));

    // Verify the prompt structure is preserved (timestamp, user@host pattern)
    QVERIFY2(screenText.contains(QStringLiteral("────")),
             qPrintable(QStringLiteral("Pane screen should contain '────' from prompt, got: ") + screenText));

    // Cleanup: close pane sessions first, then gateway
    const auto allSessions = vm->sessions();
    for (Session *s : allSessions) {
        if (s != gatewaySession) {
            s->closeInNormalWay();
        }
    }
    gatewaySession->closeInNormalWay();
    QTRY_VERIFY_WITH_TIMEOUT(!mwGuard, 10000);
    delete mwGuard.data();

    QProcess tmuxKill;
    tmuxKill.start(tmuxPath, {QStringLiteral("kill-session"), QStringLiteral("-t"), sessionName});
    tmuxKill.waitForFinished(5000);
}

QTEST_MAIN(TmuxIntegrationTest)

#include "moc_TmuxIntegrationTest.cpp"
