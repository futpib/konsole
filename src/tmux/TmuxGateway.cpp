/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TmuxGateway.h"

#include "Emulation.h"
#include "session/Session.h"

namespace Konsole
{

TmuxGateway::TmuxGateway(Session *gatewaySession, QObject *parent)
    : QObject(parent)
    , _gatewaySession(gatewaySession)
{
}

void TmuxGateway::processLine(const QByteArray &line)
{
    if (_inResponseBlock) {
        if (line.startsWith("%end ") || line.startsWith("%error ")) {
            bool success = line.startsWith("%end ");
            finishCurrentCommand(success);
            return;
        }
        // Accumulate response data
        if (!_currentCommand.response.isEmpty()) {
            _currentCommand.response += QLatin1Char('\n');
        }
        _currentCommand.response += QString::fromUtf8(line);
        return;
    }

    if (line.startsWith("%begin ")) {
        if (_pendingCommands.isEmpty()) {
            // Server-originated command; ignore
            _inResponseBlock = true;
            _currentCommand = PendingCommand();
            return;
        }
        _currentCommand = _pendingCommands.dequeue();
        _inResponseBlock = true;
        return;
    }

    if (line.startsWith("%")) {
        handleNotification(line);
    }
}

void TmuxGateway::handleNotification(const QByteArray &line)
{
    if (line.startsWith("%output ")) {
        // Format: %output %<pane-id> <octal-escaped-data>
        int firstSpace = line.indexOf(' ', 8); // after "%output "
        if (firstSpace < 0) {
            return;
        }
        QByteArray paneToken = line.mid(8, firstSpace - 8);
        int paneId = parsePaneId(paneToken);
        if (paneId < 0) {
            return;
        }
        QByteArray encoded = line.mid(firstSpace + 1);
        QByteArray decoded = decodeOctalEscapes(encoded);
        Q_EMIT outputReceived(paneId, decoded);

    } else if (line.startsWith("%layout-change ")) {
        // Format: %layout-change @<window> <layout> [<visible-layout> <flags>]
        QList<QByteArray> parts = line.mid(15).split(' ');
        if (parts.size() < 2) {
            return;
        }
        int windowId = parseWindowId(parts[0]);
        QString layout = QString::fromUtf8(parts[1]);
        QString visibleLayout;
        bool zoomed = false;
        if (parts.size() >= 3) {
            visibleLayout = QString::fromUtf8(parts[2]);
        }
        if (parts.size() >= 4) {
            // flags field: contains "*" for zoomed
            zoomed = parts[3].contains('*');
        }
        Q_EMIT layoutChanged(windowId, layout, visibleLayout, zoomed);

    } else if (line.startsWith("%window-add ")) {
        // Format: %window-add @<id>
        int windowId = parseWindowId(line.mid(12));
        Q_EMIT windowAdded(windowId);

    } else if (line.startsWith("%window-close ") || line.startsWith("%unlinked-window-close ")) {
        QByteArray rest = line.startsWith("%window-close ") ? line.mid(14) : line.mid(23);
        int windowId = parseWindowId(rest.split(' ').first());
        Q_EMIT windowClosed(windowId);

    } else if (line.startsWith("%window-renamed ")) {
        // Format: %window-renamed @<id> <name>
        QByteArray rest = line.mid(16);
        int spaceIdx = rest.indexOf(' ');
        if (spaceIdx < 0) {
            return;
        }
        int windowId = parseWindowId(rest.left(spaceIdx));
        QString name = QString::fromUtf8(rest.mid(spaceIdx + 1));
        Q_EMIT windowRenamed(windowId, name);

    } else if (line.startsWith("%window-pane-changed ")) {
        // Format: %window-pane-changed @<window> %<pane>
        QList<QByteArray> parts = line.mid(21).split(' ');
        if (parts.size() < 2) {
            return;
        }
        int windowId = parseWindowId(parts[0]);
        int paneId = parsePaneId(parts[1]);
        Q_EMIT windowPaneChanged(windowId, paneId);

    } else if (line.startsWith("%session-changed ")) {
        // Format: %session-changed $<id> <name>
        QByteArray rest = line.mid(17);
        int spaceIdx = rest.indexOf(' ');
        if (spaceIdx < 0) {
            return;
        }
        int sessionId = parseSessionId(rest.left(spaceIdx));
        QString name = QString::fromUtf8(rest.mid(spaceIdx + 1));
        Q_EMIT sessionChanged(sessionId, name);

    } else if (line.startsWith("%session-renamed ")) {
        QString name = QString::fromUtf8(line.mid(17));
        Q_EMIT sessionRenamed(name);

    } else if (line.startsWith("%sessions-changed")) {
        Q_EMIT sessionsChanged();

    } else if (line.startsWith("%session-window-changed ")) {
        // Format: %session-window-changed $<session> @<window>
        QList<QByteArray> parts = line.mid(24).split(' ');
        if (parts.size() < 2) {
            return;
        }
        int sessionId = parseSessionId(parts[0]);
        int windowId = parseWindowId(parts[1]);
        Q_EMIT sessionWindowChanged(sessionId, windowId);

    } else if (line.startsWith("%pause ")) {
        int paneId = parsePaneId(line.mid(7));
        Q_EMIT panePaused(paneId);

    } else if (line.startsWith("%continue ")) {
        int paneId = parsePaneId(line.mid(10));
        Q_EMIT paneContinued(paneId);

    } else if (line.startsWith("%exit")) {
        QString reason;
        if (line.length() > 6) {
            reason = QString::fromUtf8(line.mid(6));
        }
        Q_EMIT exitReceived(reason);
    }
}

void TmuxGateway::finishCurrentCommand(bool success)
{
    _inResponseBlock = false;
    if (_currentCommand.callback) {
        _currentCommand.callback(success, _currentCommand.response);
    }
    _currentCommand = PendingCommand();
}

void TmuxGateway::sendCommand(const QString &command, CommandCallback callback)
{
    PendingCommand cmd;
    cmd.command = command;
    cmd.callback = std::move(callback);
    _pendingCommands.enqueue(cmd);

    QByteArray data = command.toUtf8() + '\n';
    writeToGateway(data);
}

void TmuxGateway::sendKeys(int paneId, const QByteArray &data)
{
    // Split into runs of literal-safe characters vs. hex-encoded characters
    // Literal-safe: alphanumeric + selected special chars (following iTerm2)
    auto isLiteral = [](char c) -> bool {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == ')' || c == ':' || c == ','
            || c == '_';
    };

    int i = 0;
    while (i < data.size()) {
        if (isLiteral(data[i])) {
            // Collect run of literal characters (max 1000)
            QByteArray literal;
            while (i < data.size() && isLiteral(data[i]) && literal.size() < 1000) {
                literal.append(data[i]);
                i++;
            }
            QString cmd = QStringLiteral("send-keys -lt %") + QString::number(paneId) + QStringLiteral(" ") + QString::fromLatin1(literal);
            sendCommand(cmd);
        } else {
            // Collect run of hex-encoded characters (max 125)
            QStringList hexParts;
            while (i < data.size() && !isLiteral(data[i]) && hexParts.size() < 125) {
                unsigned char byte = static_cast<unsigned char>(data[i]);
                if (byte == 0) {
                    hexParts.append(QStringLiteral("C-Space"));
                } else {
                    hexParts.append(QStringLiteral("0x%1").arg(byte, 0, 16));
                }
                i++;
            }
            QString cmd = QStringLiteral("send-keys -t %") + QString::number(paneId) + QStringLiteral(" ") + hexParts.join(QLatin1Char(' '));
            sendCommand(cmd);
        }
    }
}

void TmuxGateway::detach()
{
    sendCommand(QStringLiteral("detach"));
}

QByteArray TmuxGateway::decodeOctalEscapes(const QByteArray &encoded)
{
    QByteArray result;
    result.reserve(encoded.size());

    int i = 0;
    while (i < encoded.size()) {
        char c = encoded[i];
        if (c == '\\' && i + 3 < encoded.size()) {
            // Try to read 3 octal digits
            int value = 0;
            bool valid = true;
            int j = i + 1;
            int digits = 0;
            while (digits < 3 && j < encoded.size()) {
                char d = encoded[j];
                if (d == '\r') {
                    // Skip carriage returns (tmux line driver artifact)
                    j++;
                    continue;
                }
                if (d < '0' || d > '7') {
                    valid = false;
                    break;
                }
                value = value * 8 + (d - '0');
                digits++;
                j++;
            }
            if (valid && digits == 3) {
                result.append(static_cast<char>(value));
                i = j;
            } else {
                result.append('?');
                i++;
            }
        } else if (static_cast<unsigned char>(c) < ' ' && c != '\t') {
            // Skip control characters (but not high bytes from UTF-8)
            i++;
        } else {
            result.append(c);
            i++;
        }
    }

    return result;
}

int TmuxGateway::parsePaneId(const QByteArray &token)
{
    // Format: %<number>
    if (token.isEmpty() || token[0] != '%') {
        return -1;
    }
    bool ok = false;
    int id = token.mid(1).toInt(&ok);
    return ok ? id : -1;
}

int TmuxGateway::parseWindowId(const QByteArray &token)
{
    // Format: @<number>
    QByteArray trimmed = token.trimmed();
    if (trimmed.isEmpty() || trimmed[0] != '@') {
        return -1;
    }
    bool ok = false;
    int id = trimmed.mid(1).toInt(&ok);
    return ok ? id : -1;
}

int TmuxGateway::parseSessionId(const QByteArray &token)
{
    // Format: $<number>
    if (token.isEmpty() || token[0] != '$') {
        return -1;
    }
    bool ok = false;
    int id = token.mid(1).toInt(&ok);
    return ok ? id : -1;
}

void TmuxGateway::writeToGateway(const QByteArray &data)
{
    if (_gatewaySession && _gatewaySession->emulation()) {
        Q_EMIT _gatewaySession->emulation()->sendData(data);
    }
}

} // namespace Konsole
