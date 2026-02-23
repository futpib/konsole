/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXCOMMAND_H
#define TMUXCOMMAND_H

#include <QString>
#include <QStringList>

namespace Konsole
{

class TmuxCommand
{
public:
    explicit TmuxCommand(const QString &verb)
        : _verb(verb)
    {
    }

    TmuxCommand &paneTarget(int paneId)
    {
        _parts.append(QStringLiteral("-t %%") + QString::number(paneId));
        return *this;
    }

    TmuxCommand &windowTarget(int windowId)
    {
        _parts.append(QStringLiteral("-t @") + QString::number(windowId));
        return *this;
    }

    TmuxCommand &paneSource(int paneId)
    {
        _parts.append(QStringLiteral("-s %%") + QString::number(paneId));
        return *this;
    }

    TmuxCommand &flag(const QString &f)
    {
        _parts.append(f);
        return *this;
    }

    TmuxCommand &format(const QString &fmt)
    {
        _parts.append(QStringLiteral("-F \"") + fmt + QStringLiteral("\""));
        return *this;
    }

    TmuxCommand &quotedArg(const QString &value)
    {
        _parts.append(QLatin1Char('"') + value + QLatin1Char('"'));
        return *this;
    }

    TmuxCommand &singleQuotedArg(const QString &value)
    {
        _parts.append(QLatin1Char('\'') + value + QLatin1Char('\''));
        return *this;
    }

    TmuxCommand &arg(const QString &value)
    {
        _parts.append(value);
        return *this;
    }

    QString build() const
    {
        QString result = _verb;
        for (const QString &part : _parts) {
            result += QLatin1Char(' ') + part;
        }
        return result;
    }

private:
    QString _verb;
    QStringList _parts;
};

} // namespace Konsole

#endif // TMUXCOMMAND_H
