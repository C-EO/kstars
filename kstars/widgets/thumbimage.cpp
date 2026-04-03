/*
    SPDX-FileCopyrightText: 2005 Jason Harris and Jasem Mutlaq <kstars@30doradus.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "thumbimage.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include "qtcompat.h"

QPixmap ThumbImage::croppedImage()
{
    return Image->copy(*CropRect);
}

ThumbImage::ThumbImage(QWidget *parent, const char *name) : QLabel(parent)
{
    //FIXME: Deprecated?  Maybe we don't need this, since double-buffering is now built in
    //	setBackgroundMode( Qt::WA_NoBackground );

    setObjectName(name);

    CropRect.reset(new QRect());
    Anchor.reset(new QPoint());
    Image.reset(new QPixmap());
}

void ThumbImage::paintEvent(QPaintEvent *)
{
    QPainter p;
    p.begin(this);

    p.drawPixmap(0, 0, *Image);

    p.setPen(QPen(QColor("Grey"), 2));
    p.drawRect(*CropRect);

    p.setPen(QPen(QColor("Grey"), 0));
    p.drawRect(QRect(CropRect->left(), CropRect->top(), HandleSize, HandleSize));
    p.drawRect(QRect(CropRect->right() - HandleSize, CropRect->top(), HandleSize, HandleSize));
    p.drawRect(QRect(CropRect->left(), CropRect->bottom() - HandleSize, HandleSize, HandleSize));
    p.drawRect(QRect(CropRect->right() - HandleSize, CropRect->bottom() - HandleSize, HandleSize, HandleSize));

    if (CropRect->x() > 0)
        p.fillRect(0, 0, CropRect->x(), height(), QBrush(QColor("white"), Qt::Dense3Pattern));
    if (CropRect->right() < width())
        p.fillRect(CropRect->right(), 0, (width() - CropRect->right()), height(),
                   QBrush(QColor("white"), Qt::Dense3Pattern));
    if (CropRect->y() > 0)
        p.fillRect(0, 0, width(), CropRect->y(), QBrush(QColor("white"), Qt::Dense3Pattern));
    if (CropRect->bottom() < height())
        p.fillRect(0, CropRect->bottom(), width(), (height() - CropRect->bottom()),
                   QBrush(QColor("white"), Qt::Dense3Pattern));

    p.end();
}

void ThumbImage::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = QtCompat::mousePos(e).toPoint();
    if (e->button() == Qt::LeftButton && CropRect->contains(pos))
    {
        bMouseButtonDown = true;

        //The Anchor tells how far from the CropRect corner we clicked
        Anchor->setX(pos.x() - CropRect->left());
        Anchor->setY(pos.y() - CropRect->top());

        if (pos.x() <= CropRect->left() + HandleSize && pos.y() <= CropRect->top() + HandleSize)
        {
            bTopLeftGrab = true;
        }
        if (pos.x() <= CropRect->left() + HandleSize && pos.y() >= CropRect->bottom() - HandleSize)
        {
            bBottomLeftGrab = true;
            Anchor->setY(pos.y() - CropRect->bottom());
        }
        if (pos.x() >= CropRect->right() - HandleSize && pos.y() <= CropRect->top() + HandleSize)
        {
            bTopRightGrab = true;
            Anchor->setX(pos.x() - CropRect->right());
        }
        if (pos.x() >= CropRect->right() - HandleSize && pos.y() >= CropRect->bottom() - HandleSize)
        {
            bBottomRightGrab = true;
            Anchor->setX(pos.x() - CropRect->right());
            Anchor->setY(pos.y() - CropRect->bottom());
        }
    }
}

void ThumbImage::mouseReleaseEvent(QMouseEvent *)
{
    if (bMouseButtonDown)
        bMouseButtonDown = false;
    if (bTopLeftGrab)
        bTopLeftGrab = false;
    if (bTopRightGrab)
        bTopRightGrab = false;
    if (bBottomLeftGrab)
        bBottomLeftGrab = false;
    if (bBottomRightGrab)
        bBottomRightGrab = false;
}

void ThumbImage::mouseMoveEvent(QMouseEvent *e)
{
    if (bMouseButtonDown)
    {
        const QPoint pos = QtCompat::mousePos(e).toPoint();
        //If a corner was grabbed, we are resizing the box
        if (bTopLeftGrab)
        {
            if (pos.x() >= 0 && pos.x() <= width())
                CropRect->setLeft(pos.x() - Anchor->x());
            if (pos.y() >= 0 && pos.y() <= height())
                CropRect->setTop(pos.y() - Anchor->y());
            if (CropRect->left() < 0)
                CropRect->setLeft(0);
            if (CropRect->top() < 0)
                CropRect->setTop(0);
            if (CropRect->width() < 200)
                CropRect->setLeft(CropRect->left() - 200 + CropRect->width());
            if (CropRect->height() < 200)
                CropRect->setTop(CropRect->top() - 200 + CropRect->height());
        }
        else if (bTopRightGrab)
        {
            if (pos.x() >= 0 && pos.x() <= width())
                CropRect->setRight(pos.x() - Anchor->x());
            if (pos.y() >= 0 && pos.y() <= height())
                CropRect->setTop(pos.y() - Anchor->y());
            if (CropRect->right() > width())
                CropRect->setRight(width());
            if (CropRect->top() < 0)
                CropRect->setTop(0);
            if (CropRect->width() < 200)
                CropRect->setRight(CropRect->right() + 200 - CropRect->width());
            if (CropRect->height() < 200)
                CropRect->setTop(CropRect->top() - 200 + CropRect->height());
        }
        else if (bBottomLeftGrab)
        {
            if (pos.x() >= 0 && pos.x() <= width())
                CropRect->setLeft(pos.x() - Anchor->x());
            if (pos.y() >= 0 && pos.y() <= height())
                CropRect->setBottom(pos.y() - Anchor->y());
            if (CropRect->left() < 0)
                CropRect->setLeft(0);
            if (CropRect->bottom() > height())
                CropRect->setBottom(height());
            if (CropRect->width() < 200)
                CropRect->setLeft(CropRect->left() - 200 + CropRect->width());
            if (CropRect->height() < 200)
                CropRect->setBottom(CropRect->bottom() + 200 - CropRect->height());
        }
        else if (bBottomRightGrab)
        {
            if (pos.x() >= 0 && pos.x() <= width())
                CropRect->setRight(pos.x() - Anchor->x());
            if (pos.y() >= 0 && pos.y() <= height())
                CropRect->setBottom(pos.y() - Anchor->y());
            if (CropRect->right() > width())
                CropRect->setRight(width());
            if (CropRect->bottom() > height())
                CropRect->setBottom(height());
            if (CropRect->width() < 200)
                CropRect->setRight(CropRect->right() + 200 - CropRect->width());
            if (CropRect->height() < 200)
                CropRect->setBottom(CropRect->bottom() + 200 - CropRect->height());
        }
        else //no corner grabbed; move croprect
        {
            CropRect->moveTopLeft(QPoint(pos.x() - Anchor->x(), pos.y() - Anchor->y()));
            if (CropRect->left() < 0)
                CropRect->moveLeft(0);
            if (CropRect->right() > width())
                CropRect->moveRight(width());
            if (CropRect->top() < 0)
                CropRect->moveTop(0);
            if (CropRect->bottom() > height())
                CropRect->moveBottom(height());
        }

        emit cropRegionModified();
        update();
    }
}
