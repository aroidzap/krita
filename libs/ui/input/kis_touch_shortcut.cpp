/*
 *  This file is part of the KDE project
 *  SPDX-FileCopyrightText: 2012 Arjen Hiemstra <ahiemstra@heimr.nl>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "kis_touch_shortcut.h"
#include "kis_abstract_input_action.h"

#include <QGuiApplication>
#include <QTouchEvent>
#include <QScreen>
#include <QWindow>
#include <QLine>

class KisTouchShortcut::Private
{
public:
    Private(GestureAction type)
        : minTouchPoints(0)
        , maxTouchPoints(0)
        , type(type)
    { }

    int minTouchPoints;
    int maxTouchPoints;
    GestureAction type;
};

KisTouchShortcut::KisTouchShortcut(KisAbstractInputAction* action, int index, GestureAction type)
    : KisAbstractShortcut(action, index)
    , d(new Private(type))
{

}

KisTouchShortcut::~KisTouchShortcut()
{
    delete d;
}

int KisTouchShortcut::priority() const
{
    return action()->priority();
}

void KisTouchShortcut::setMinimumTouchPoints(int min)
{
    d->minTouchPoints = min;
}

void KisTouchShortcut::setMaximumTouchPoints(int max)
{
    d->maxTouchPoints = max;
}

bool KisTouchShortcut::matchType(QTouchEvent *event,
    bool isTap, bool isHold, bool isDrag, 
    bool isHorizontalDrag, bool isVerticalDrag)
{
#ifdef Q_OS_MACOS
    return matchTouchPoint(event);
#else
    if (matchTouchPoint(event)) {
        using KSC = KisShortcutConfiguration;
        bool match = false;
        if ((isTap || isHold) && event->touchPoints().count()) 
        {
            const float dpi = QGuiApplication::primaryScreen()->physicalDotsPerInch();
            const float cornerTouchDist = 0.5 * dpi;
            QPointF touchPos = event->touchPoints().front().startPos();
            float width = static_cast<float>(event->window()->width());
            float height = static_cast<float>(event->window()->height());

            if (QLineF(touchPos, QPointF{0.f, 0.f}).length() < cornerTouchDist) {
                match |= isTap && d->type == KSC::UpperLeftCornerTap;
                match |= isHold && d->type == KSC::UpperLeftCornerHold;
            } else if (QLineF(touchPos, QPointF{0.f, width}).length() < cornerTouchDist) {
                match |= isTap && d->type == KSC::UpperRightCornerTap;
                match |= isHold && d->type == KSC::UpperRightCornerHold;
            } else if (QLineF(touchPos, QPointF{height, 0.f}).length() < cornerTouchDist) {
                match |= isTap && d->type == KSC::LowerLeftCornerTap;
                match |= isHold && d->type == KSC::LowerLeftCornerHold;
            } else if (QLineF(touchPos, QPointF{height, width}).length() < cornerTouchDist) {
                match |= isTap && d->type == KSC::LowerRightCornerTap;
                match |= isHold && d->type == KSC::LowerRightCornerHold;
            }
        }
        match |= isTap && 
            d->type >= KSC::OneFingerTap && d->type <= KSC::OneFingerTap;
        match |= isHold && 
            d->type >= KSC::OneFingerHold && d->type <= KSC::OneFingerHold;
        match |= isDrag && 
            d->type >= KSC::OneFingerDrag && d->type <= KSC::FiveFingerDrag;
        match |= isHorizontalDrag && 
            d->type >= KSC::OneFingerDragHorizontal && d->type <= KSC::FiveFingerDragHorizontal;
        match |= isVerticalDrag && 
            d->type >= KSC::OneFingerDragVertical && d->type <= KSC::FiveFingerDragVertical;
        return match;
    } else {
        return false;
    }
#endif
}

bool KisTouchShortcut::matchTouchPoint(QTouchEvent *event)
{
    return event->touchPoints().count() >= d->minTouchPoints && event->touchPoints().count() <= d->maxTouchPoints;
}
