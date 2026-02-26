/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TABPAGEWIDGET_H
#define TABPAGEWIDGET_H

#include <QSize>
#include <QWidget>

#include "konsoleprivate_export.h"

namespace Konsole
{

class ViewSplitter;

/**
 * A thin wrapper widget placed between QTabWidget and ViewSplitter.
 *
 * In normal (unconstrained) mode the child ViewSplitter fills the
 * entire page.  When a tmux layout constraint is active the splitter
 * is positioned at the top-left corner at a fixed pixel size and the
 * remaining area is left empty.
 */
class KONSOLEPRIVATE_EXPORT TabPageWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TabPageWidget(ViewSplitter *splitter, QWidget *parent = nullptr);

    ViewSplitter *splitter() const
    {
        return _splitter;
    }

    /** Constrain the child splitter to the given pixel size (top-left). */
    void setConstrainedSize(const QSize &size);

    /** Remove the constraint; the splitter fills the page again. */
    void clearConstrainedSize();

    bool isConstrained() const
    {
        return _constrained;
    }

    QSize constrainedSize() const
    {
        return _constrainedSize;
    }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutChild();

    ViewSplitter *_splitter = nullptr;
    bool _constrained = false;
    QSize _constrainedSize;
};

} // namespace Konsole

#endif // TABPAGEWIDGET_H
