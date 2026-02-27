/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxPaneStateRecovery.h"

#include "TmuxCommand.h"
#include "TmuxGateway.h"
#include "TmuxPaneManager.h"

#include "Emulation.h"
#include "session/VirtualSession.h"

namespace Konsole
{

TmuxPaneStateRecovery::TmuxPaneStateRecovery(TmuxGateway *gateway, TmuxPaneManager *paneManager, QObject *parent)
    : QObject(parent)
    , _gateway(gateway)
    , _paneManager(paneManager)
{
}

void TmuxPaneStateRecovery::queryPaneStates(int windowId)
{
    static const QString format = QStringLiteral(
        "#{pane_id}\t#{alternate_on}\t#{cursor_x}\t#{cursor_y}"
        "\t#{scroll_region_upper}\t#{scroll_region_lower}"
        "\t#{cursor_flag}\t#{insert_flag}\t#{keypad_cursor_flag}"
        "\t#{keypad_flag}\t#{wrap_flag}\t#{mouse_standard_flag}"
        "\t#{mouse_button_flag}\t#{mouse_any_flag}\t#{mouse_sgr_flag}");

    _gateway->sendCommand(TmuxCommand(QStringLiteral("list-panes"))
                              .windowTarget(windowId)
                              .format(format),
                          [this, windowId](bool success, const QString &response) {
                              handlePaneStateResponse(windowId, success, response);
                          });
}

void TmuxPaneStateRecovery::handlePaneStateResponse(int windowId, bool success, const QString &response)
{
    Q_UNUSED(windowId)

    if (!success || response.isEmpty()) {
        return;
    }

    const QStringList lines = response.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 15) {
            continue;
        }

        QString paneIdStr = fields[0];
        if (!paneIdStr.startsWith(QLatin1Char('%'))) {
            continue;
        }
        int paneId = paneIdStr.mid(1).toInt();

        TmuxPaneState state;
        state.paneId = paneId;
        state.alternateOn = fields[1] == QLatin1String("1");
        state.cursorX = fields[2].toInt();
        state.cursorY = fields[3].toInt();
        state.scrollRegionUpper = fields[4].toInt();
        state.scrollRegionLower = fields[5].toInt();
        state.cursorVisible = fields[6] == QLatin1String("1");
        state.insertMode = fields[7] == QLatin1String("1");
        state.appCursorKeys = fields[8] == QLatin1String("1");
        state.appKeypad = fields[9] == QLatin1String("1");
        state.wrapMode = fields[10] == QLatin1String("1");
        state.mouseStandard = fields[11] == QLatin1String("1");
        state.mouseButton = fields[12] == QLatin1String("1");
        state.mouseAny = fields[13] == QLatin1String("1");
        state.mouseSGR = fields[14] == QLatin1String("1");

        _paneStates[paneId] = state;
    }
}

void TmuxPaneStateRecovery::setPaneDimensions(int paneId, int width, int height)
{
    _paneDimensions[paneId] = qMakePair(width, height);
}

void TmuxPaneStateRecovery::capturePaneHistory(int paneId)
{
    _pendingCapture.insert(paneId);
    _gateway->sendCommand(TmuxCommand(QStringLiteral("capture-pane"))
                              .flag(QStringLiteral("-p"))
                              .flag(QStringLiteral("-J"))
                              .flag(QStringLiteral("-e"))
                              .paneTarget(paneId)
                              .flag(QStringLiteral("-S"))
                              .arg(QStringLiteral("-")),
                          [this, paneId](bool success, const QString &response) {
                              handleCapturePaneResponse(paneId, success, response);
                          });
}

bool TmuxPaneStateRecovery::isPendingCapture(int paneId) const
{
    return _pendingCapture.contains(paneId);
}

void TmuxPaneStateRecovery::handleCapturePaneResponse(int paneId, bool success, const QString &response)
{
    _pendingCapture.remove(paneId);

    if (!success || response.isEmpty()) {
        Q_EMIT paneRecoveryComplete(paneId);
        return;
    }

    auto *session = qobject_cast<VirtualSession *>(_paneManager->sessionForPane(paneId));
    if (!session) {
        Q_EMIT paneRecoveryComplete(paneId);
        return;
    }

    // Set the emulation screen size to match the tmux pane dimensions
    // before injecting content, so long lines wrap at the correct column
    if (_paneDimensions.contains(paneId)) {
        auto dims = _paneDimensions.take(paneId);
        session->emulation()->setImageSize(dims.second, dims.first);
    }

    // Clear any garbled content from %output that arrived before the
    // emulation was sized correctly
    static const char clearSeq[] = "\033[2J\033[H";
    session->injectData(clearSeq, sizeof(clearSeq) - 1);

    QStringList lines = response.split(QLatin1Char('\n'));

    // Trim trailing empty lines — capture-pane pads to the pane height
    // which would push real content off-screen
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
        lines.removeLast();
    }

    for (int i = 0; i < lines.size(); ++i) {
        QByteArray lineData = lines[i].toUtf8();
        if (i < lines.size() - 1) {
            lineData.append("\r\n");
        }
        session->injectData(lineData.constData(), lineData.size());
    }

    applyPaneState(paneId);
    Q_EMIT paneRecoveryComplete(paneId);
}

void TmuxPaneStateRecovery::applyPaneState(int paneId)
{
    if (!_paneStates.contains(paneId)) {
        return;
    }

    auto *session = qobject_cast<VirtualSession *>(_paneManager->sessionForPane(paneId));
    if (!session) {
        return;
    }

    const TmuxPaneState &state = _paneStates[paneId];
    QByteArray seq;

    if (state.alternateOn) {
        seq.append("\033[?1049h");
    }

    if (state.scrollRegionUpper != 0 || state.scrollRegionLower != -1) {
        int lower = state.scrollRegionLower;
        if (lower < 0) {
            lower = 9999;
        }
        seq.append(QStringLiteral("\033[%1;%2r").arg(state.scrollRegionUpper + 1).arg(lower + 1).toUtf8());
    }

    seq.append(QStringLiteral("\033[%1;%2H").arg(state.cursorY + 1).arg(state.cursorX + 1).toUtf8());

    if (!state.cursorVisible) {
        seq.append("\033[?25l");
    }

    if (state.insertMode) {
        seq.append("\033[4h");
    }

    if (state.appCursorKeys) {
        seq.append("\033[?1h");
    }

    if (state.appKeypad) {
        seq.append("\033=");
    }

    if (!state.wrapMode) {
        seq.append("\033[?7l");
    }

    if (state.mouseStandard) {
        seq.append("\033[?1000h");
    }
    if (state.mouseButton) {
        seq.append("\033[?1002h");
    }
    if (state.mouseAny) {
        seq.append("\033[?1003h");
    }
    if (state.mouseSGR) {
        seq.append("\033[?1006h");
    }

    if (!seq.isEmpty()) {
        session->injectData(seq.constData(), seq.size());
    }

    _paneStates.remove(paneId);
}

void TmuxPaneStateRecovery::clear()
{
    _paneStates.clear();
}

} // namespace Konsole

#include "moc_TmuxPaneStateRecovery.cpp"
