/*
    SPDX-FileCopyrightText: 2025 Konsole contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widgets/TabPageWidget.h"

#include "widgets/ViewSplitter.h"

#include <QResizeEvent>

namespace Konsole
{

TabPageWidget::TabPageWidget(ViewSplitter *splitter, QWidget *parent)
    : QWidget(parent)
    , _splitter(splitter)
{
    splitter->setParent(this);
    connect(splitter, &QObject::destroyed, this, [this]() {
        _splitter = nullptr;
        deleteLater();
    });
    layoutChild();
}

void TabPageWidget::setConstrainedSize(const QSize &size)
{
    _constrained = true;
    _constrainedSize = size;
    layoutChild();
}

void TabPageWidget::clearConstrainedSize()
{
    _constrained = false;
    _constrainedSize = QSize();
    layoutChild();
}

void TabPageWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChild();
}

void TabPageWidget::layoutChild()
{
    if (!_splitter) {
        return;
    }

    if (_constrained) {
        int w = qMin(_constrainedSize.width(), width());
        int h = qMin(_constrainedSize.height(), height());
        _splitter->setGeometry(0, 0, w, h);
    } else {
        _splitter->setGeometry(0, 0, width(), height());
    }
}

} // namespace Konsole

#include "moc_TabPageWidget.cpp"
