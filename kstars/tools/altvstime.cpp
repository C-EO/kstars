/*
    SPDX-FileCopyrightText: 2002-2003 Pablo de Vicente <vicente@oan.es>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "altvstime.h"

#include "avtplotwidget.h"
#include "dms.h"
#include "ksalmanac.h"
#include "kstarsdata.h"
#include "kstarsdatetime.h"
#include "ksnumbers.h"
#include "simclock.h"
#include "kssun.h"
#include "dialogs/finddialog.h"
#include "dialogs/locationdialog.h"
#include "geolocation.h"
#include "skyobjects/skypoint.h"
#include "skyobjects/skyobject.h"
#include "skyobjects/starobject.h"

#include <KLocalizedString>
#include <kplotwidget.h>

#include <QVBoxLayout>
#include <QFrame>
#include <QDialog>
#include <QPainter>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QToolTip>

#include "kstars_debug.h"

AltVsTimeUI::AltVsTimeUI(QWidget *p) : QFrame(p)
{
    setupUi(this);
}

AltVsTime::AltVsTime(QWidget *parent) : QDialog(parent)
{
#ifdef Q_OS_MACOS
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
#endif

    setWindowTitle(i18nc("@title:window", "Altitude vs. Time"));

    setModal(false);

    QVBoxLayout *topLayout = new QVBoxLayout;
    setLayout(topLayout);
    topLayout->setContentsMargins(0, 0, 0, 0);
    avtUI = new AltVsTimeUI(this);

    // Layers for setting up the plot's priority: the current curve should be above the other curves.
    // The Rise/Set/Transit markers should be on top, with highest priority.
    avtUI->View->addLayer("currentCurveLayer", avtUI->View->layer("main"), QCustomPlot::limAbove);
    avtUI->View->addLayer("markersLayer", avtUI->View->layer("currentCurveLayer"), QCustomPlot::limAbove);

    // Set up the Graph Window:
    avtUI->View->setBackground(QBrush(QColor(0, 0, 0)));
    avtUI->View->xAxis->grid()->setVisible(false);
    avtUI->View->yAxis->grid()->setVisible(false);
    QColor axisColor(Qt::white);
    QPen axisPen(axisColor, 1);
    avtUI->View->xAxis->setBasePen(axisPen);
    avtUI->View->xAxis->setTickPen(axisPen);
    avtUI->View->xAxis->setSubTickPen(axisPen);
    avtUI->View->xAxis->setTickLabelColor(axisColor);
    avtUI->View->xAxis->setLabelColor(axisColor);

    avtUI->View->xAxis2->setBasePen(axisPen);
    avtUI->View->xAxis2->setTickPen(axisPen);
    avtUI->View->xAxis2->setSubTickPen(axisPen);
    avtUI->View->xAxis2->setTickLabelColor(axisColor);
    avtUI->View->xAxis2->setLabelColor(axisColor);

    avtUI->View->yAxis->setBasePen(axisPen);
    avtUI->View->yAxis->setTickPen(axisPen);
    avtUI->View->yAxis->setSubTickPen(axisPen);
    avtUI->View->yAxis->setTickLabelColor(axisColor);
    avtUI->View->yAxis->setLabelColor(axisColor);

    avtUI->View->yAxis2->setBasePen(axisPen);
    avtUI->View->yAxis2->setTickPen(axisPen);
    avtUI->View->yAxis2->setSubTickPen(axisPen);
    avtUI->View->yAxis2->setTickLabelColor(axisColor);
    avtUI->View->yAxis2->setLabelColor(axisColor);

    // give the axis some labels:
    avtUI->View->xAxis2->setLabel(i18n("Local Sidereal Time"));
    avtUI->View->xAxis2->setVisible(true);
    avtUI->View->yAxis2->setVisible(true);
    avtUI->View->yAxis2->setTickLength(0, 0);
    avtUI->View->xAxis->setLabel(i18n("Local Time"));
    avtUI->View->yAxis->setLabel(i18n("Altitude"));
    avtUI->View->xAxis->setRange(43200, 129600);
    avtUI->View->xAxis2->setRange(61200, 147600);

    // configure the bottom axis to show time instead of number:
    QSharedPointer<QCPAxisTickerTime> xAxisTimeTicker(new QCPAxisTickerTime);
    xAxisTimeTicker->setTimeFormat("%h:%m");
    xAxisTimeTicker->setTickCount(12);
    xAxisTimeTicker->setTickStepStrategy(QCPAxisTicker::tssReadability);
    xAxisTimeTicker->setTickOrigin(Qt::UTC);
    avtUI->View->xAxis->setTicker(xAxisTimeTicker);

    // configure the top axis to show time instead of number:
    QSharedPointer<QCPAxisTickerTime> xAxis2TimeTicker(new QCPAxisTickerTime);
    xAxis2TimeTicker->setTimeFormat("%h:%m");
    xAxis2TimeTicker->setTickCount(12);
    xAxis2TimeTicker->setTickStepStrategy(QCPAxisTicker::tssReadability);
    xAxis2TimeTicker->setTickOrigin(Qt::UTC);
    avtUI->View->xAxis2->setTicker(xAxis2TimeTicker);

    // set up the Zoom/Pan features for the Top X Axis
    avtUI->View->axisRect()->setRangeDragAxes(avtUI->View->xAxis2, avtUI->View->yAxis);
    avtUI->View->axisRect()->setRangeZoomAxes(avtUI->View->xAxis2, avtUI->View->yAxis);

    // set up the margins, for a nice view
    avtUI->View->axisRect()->setAutoMargins(QCP::msBottom | QCP::msLeft | QCP::msTop);
    avtUI->View->axisRect()->setMargins(QMargins(0, 0, 7, 0));

    // set up the interaction set:
    avtUI->View->setInteraction(QCP::iRangeZoom, true);
    avtUI->View->setInteraction(QCP::iRangeDrag, true);

    // set up the selection tolerance for checking if a certain graph is or not selected:
    avtUI->View->setSelectionTolerance(5);

    // draw the gradient:
    drawGradient();

    // set up the background image:
    background = new QCPItemPixmap(avtUI->View);
    background->setPixmap(*gradient);
    background->topLeft->setType(QCPItemPosition::ptPlotCoords);
    background->bottomRight->setType(QCPItemPosition::ptPlotCoords);
    background->setScaled(true, Qt::IgnoreAspectRatio);
    background->setLayer("background");
    background->setVisible(true);

    avtUI->raBox->setUnits(dmsBox::HOURS);
    avtUI->decBox->setUnits(dmsBox::DEGREES);

    //FIXME:
    //Doesn't make sense to manually adjust long/lat unless we can modify TZ also
    avtUI->longBox->setReadOnly(true);
    avtUI->latBox->setReadOnly(true);

    topLayout->addWidget(avtUI);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    topLayout->addWidget(buttonBox);
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));

    QPushButton *printB = new QPushButton(QIcon::fromTheme("document-print"), i18n("&Print..."));
    printB->setToolTip(i18n("Print the Altitude vs. time plot"));
    buttonBox->addButton(printB, QDialogButtonBox::ActionRole);
    connect(printB, SIGNAL(clicked()), this, SLOT(slotPrint()));

    geo = KStarsData::Instance()->geo();

    DayOffset = 0;
    // set up the initial minimum and maximum altitude
    minAlt = 0;
    maxAlt = 0;
    showCurrentDate();
    if (getDate().time().hour() > 12)
        DayOffset = 1;

    avtUI->longBox->show(geo->lng());
    avtUI->latBox->show(geo->lat());

    //computeSunRiseSetTimes();
    //setLSTLimits();
    //setDawnDusk();

    connect(avtUI->View->yAxis, SIGNAL(rangeChanged(QCPRange)), this, SLOT(onYRangeChanged(QCPRange)));
    connect(avtUI->View->xAxis2, SIGNAL(rangeChanged(QCPRange)), this, SLOT(onXRangeChanged(QCPRange)));
    connect(avtUI->View, SIGNAL(plottableClick(QCPAbstractPlottable*, int, QMouseEvent*)), this,
            SLOT(plotMousePress(QCPAbstractPlottable*, int, QMouseEvent*)));
    connect(avtUI->View, SIGNAL(mouseMove(QMouseEvent*)), this, SLOT(mouseOverLine(QMouseEvent*)));

    connect(avtUI->browseButton, SIGNAL(clicked()), this, SLOT(slotBrowseObject()));
    connect(avtUI->cityButton, SIGNAL(clicked()), this, SLOT(slotChooseCity()));
    connect(avtUI->updateButton, SIGNAL(clicked()), this, SLOT(slotUpdateDateLoc()));
    connect(avtUI->clearButton, SIGNAL(clicked()), this, SLOT(slotClear()));
    connect(avtUI->addButton, SIGNAL(clicked()), this, SLOT(slotAddSource()));
    connect(avtUI->nameBox, SIGNAL(returnPressed()), this, SLOT(slotAddSource()));
    connect(avtUI->raBox, SIGNAL(returnPressed()), this, SLOT(slotAddSource()));
    connect(avtUI->decBox, SIGNAL(returnPressed()), this, SLOT(slotAddSource()));
    connect(avtUI->clearFieldsButton, SIGNAL(clicked()), this, SLOT(slotClearBoxes()));
    connect(avtUI->longBox, SIGNAL(returnPressed()), this, SLOT(slotAdvanceFocus()));
    connect(avtUI->latBox, SIGNAL(returnPressed()), this, SLOT(slotAdvanceFocus()));
    connect(avtUI->PlotList, SIGNAL(currentRowChanged(int)), this, SLOT(slotHighlight(int)));
    connect(avtUI->computeButton, SIGNAL(clicked()), this, SLOT(slotComputeAltitudeByTime()));
    connect(avtUI->riseButton, SIGNAL(clicked()), this, SLOT(slotMarkRiseTime()));
    connect(avtUI->setButton, SIGNAL(clicked()), this, SLOT(slotMarkSetTime()));
    connect(avtUI->transitButton, SIGNAL(clicked()), this, SLOT(slotMarkTransitTime()));

    // Set up the Rise/Set/Transit buttons' icons:

    QPixmap redButton(100, 100);
    redButton.fill(Qt::transparent);
    QPainter p;
    p.begin(&redButton);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(Qt::red, 2);
    p.setPen(pen);
    QBrush brush(Qt::red);
    p.setBrush(brush);
    p.drawEllipse(15, 15, 80, 80);
    p.end();

    QPixmap blueButton(100, 100);
    blueButton.fill(Qt::transparent);
    QPainter p1;
    p1.begin(&blueButton);
    p1.setRenderHint(QPainter::Antialiasing, true);
    QPen pen1(Qt::blue, 2);
    p1.setPen(pen1);
    QBrush brush1(Qt::blue);
    p1.setBrush(brush1);
    p1.drawEllipse(15, 15, 80, 80);
    p1.end();

    QPixmap greenButton(100, 100);
    greenButton.fill(Qt::transparent);
    QPainter p2;
    p2.begin(&greenButton);
    p2.setRenderHint(QPainter::Antialiasing, true);
    QPen pen2(Qt::green, 2);
    p2.setPen(pen2);
    QBrush brush2(Qt::green);
    p2.setBrush(brush2);
    p2.drawEllipse(15, 15, 80, 80);
    p2.end();

    avtUI->riseButton->setIcon(QIcon(redButton));
    avtUI->setButton->setIcon(QIcon(blueButton));
    avtUI->transitButton->setIcon(QIcon(greenButton));

    setMouseTracking(true);
}

AltVsTime::~AltVsTime()
{
    //WARNING: need to delete deleteList items!
}
void AltVsTime::slotAddSource()
{

    SkyObject *obj = KStarsData::Instance()->objectNamed(avtUI->nameBox->text());
    if (!obj)
    {
        QString name = FindDialog::processSearchText(avtUI->nameBox->text());
        obj = KStarsData::Instance()->objectNamed(name);
    }
    if (obj)
    {
        //An object with the current name exists.  If the object is not already
        //in the avt list, add it.
        bool found = false;
        foreach (SkyObject *o, pList)
        {
            //if ( o->name() == obj->name() ) {
            if (getObjectName(o, false) == getObjectName(obj, false))
            {
                found = true;
                break;
            }
        }
        if (!found)
            processObject(obj);
    }
    else
    {
        //Object with the current name doesn't exist.  It's possible that the
        //user is trying to add a custom object.  Assume this is the case if
        //the RA and Dec fields are filled in.

        if (!avtUI->nameBox->text().isEmpty() && !avtUI->raBox->text().isEmpty() && !avtUI->decBox->text().isEmpty())
        {
            bool okRA, okDec;
            dms newRA  = avtUI->raBox->createDms(&okRA);
            dms newDec = avtUI->decBox->createDms(&okDec);
            if (!okRA || !okDec)
                return;

            //If the epochName is blank (or any non-double), we assume J2000
            //Otherwise, precess to J2000.
            KStarsDateTime dt;
            dt.setFromEpoch(getEpoch(avtUI->epochName->text()));
            long double jd = dt.djd();
            if (jd != J2000)
            {
                SkyPoint ptest(newRA, newDec);
                //ptest.precessFromAnyEpoch(jd, J2000);
                ptest.catalogueCoord(jd);
                newRA.setH(ptest.ra().Hours());
                newDec.setD(ptest.dec().Degrees());
            }

            //make sure the coords do not already exist from another object
            bool found = false;
            foreach (SkyObject *p, pList)
            {
                //within an arcsecond?
                if (fabs(newRA.Degrees() - p->ra().Degrees()) < 0.0003 &&
                        fabs(newDec.Degrees() - p->dec().Degrees()) < 0.0003)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                SkyObject *obj = new SkyObject(8, newRA, newDec, 1.0, avtUI->nameBox->text());
                deleteList.append(obj); //this object will be deleted when window is destroyed
                processObject(obj);
            }
        }

        //If the Ra and Dec boxes are filled, but the name field is empty,
        //move input focus to nameBox.  If either coordinate box is empty,
        //move focus there
        if (avtUI->nameBox->text().isEmpty())
        {
            avtUI->nameBox->QWidget::setFocus();
        }
        if (avtUI->raBox->text().isEmpty())
        {
            avtUI->raBox->QWidget::setFocus();
        }
        else
        {
            if (avtUI->decBox->text().isEmpty())
                avtUI->decBox->QWidget::setFocus();
        }
    }

    avtUI->View->update();
}

//Use find dialog to choose an object
void AltVsTime::slotBrowseObject()
{
    if (FindDialog::Instance()->exec() == QDialog::Accepted)
    {
        SkyObject *o = FindDialog::Instance()->targetObject();
        processObject(o);
    }

    avtUI->View->update();
    avtUI->View->replot();
}

void AltVsTime::processObject(SkyObject *o, bool forceAdd)
{
    if (!o)
        return;

    KSNumbers *num    = new KSNumbers(getDate().djd());
    KSNumbers *oldNum = nullptr;

    //If the object is in the solar system, recompute its position for the given epochLabel
    KStarsData *data = KStarsData::Instance();
    if (o->isSolarSystem())
    {
        oldNum = new KSNumbers(data->ut().djd());
        o->updateCoords(num, true, geo->lat(), data->lst(), true);
    }

    //precess coords to target epoch
    o->updateCoordsNow(num);

    // vector used for computing the points needed for drawing the graph
    QVector<double> y(100), t(100);

    //If this point is not in list already, add it to list
    bool found(false);
    foreach (SkyObject *p, pList)
    {
        if (o->ra().Degrees() == p->ra().Degrees() && o->dec().Degrees() == p->dec().Degrees())
        {
            found = true;
            break;
        }
    }
    if (found && !forceAdd)
    {
        qCWarning(KSTARS) << "This point is already displayed; It will not be duplicated.";
    }
    else
    {
        pList.append(o);

        // make sure existing curves are thin and red:

        for (int i = 0; i < avtUI->View->graphCount(); i++)
        {
            if (avtUI->View->graph(i)->pen().color() == Qt::white)
            {
                avtUI->View->graph(i)->setPen(QPen(Qt::red, 2));
            }
        }

        // SET up the curve's name
        avtUI->View->addGraph()->setName(o->name());

        // compute the current graph:
        // time range: 24h

        int offset = 3;
        for (double h = -12.0, i = 0; h <= 12.0; h += 0.25, i++)
        {
            y[i] = findAltitude(o, h);
            if (y[i] > maxAlt)
                maxAlt = y[i];
            if (y[i] < minAlt)
                minAlt = y[i];
            t[i] = i * 900 + 43200;
            avtUI->View->graph(avtUI->View->graphCount() - 1)->addData(t[i], y[i]);
        }
        avtUI->View->graph(avtUI->View->graphCount() - 1)->setPen(QPen(Qt::white, 3));

        // Go into initial state: without Zoom/Pan
        avtUI->View->xAxis->setRange(43200, 129600);
        avtUI->View->xAxis2->setRange(61200, 147600);
        if (abs(minAlt) > maxAlt)
            maxAlt = abs(minAlt);
        else
            minAlt = -maxAlt;

        avtUI->View->yAxis->setRange(minAlt - offset, maxAlt + offset);

        // Update background coordinates:
        background->topLeft->setCoords(avtUI->View->xAxis->range().lower, avtUI->View->yAxis->range().upper);
        background->bottomRight->setCoords(avtUI->View->xAxis->range().upper, avtUI->View->yAxis->range().lower);

        avtUI->View->replot();

        avtUI->PlotList->addItem(getObjectName(o));
        avtUI->PlotList->setCurrentRow(avtUI->PlotList->count() - 1);
        avtUI->raBox->show(o->ra());
        avtUI->decBox->show(o->dec());
        avtUI->nameBox->setText(getObjectName(o));

        //Set epochName to epoch shown in date tab
        avtUI->epochName->setText(QString().setNum(getDate().epoch()));
    }
    //qCDebug() << "Currently, there are " << avtUI->View->graphCount() << " objects displayed.";

    //restore original position
    if (o->isSolarSystem())
    {
        o->updateCoords(oldNum, true, data->geo()->lat(), data->lst(), true);
        delete oldNum;
    }
    o->EquatorialToHorizontal(data->lst(), data->geo()->lat());
    delete num;
}

double AltVsTime::findAltitude(SkyPoint *p, double hour)
{
    hour += 24.0 * DayOffset;

    //getDate converts the user-entered local time to UT
    KStarsDateTime ut = getDate().addSecs(hour * 3600.0);

    CachingDms LST = geo->GSTtoLST(ut.gst());
    p->EquatorialToHorizontal(&LST, geo->lat());
    return p->alt().Degrees();
}

void AltVsTime::slotHighlight(int row)
{
    if (row < 0)
        return;

    int rowIndex = 0;
    //highlight the curve of the selected object
    for (int i = 0; i < avtUI->View->graphCount(); i++)
    {
        if (i == row)
            rowIndex = row;
        else
        {
            avtUI->View->graph(i)->setPen(QPen(Qt::red, 2));
            avtUI->View->graph(i)->setLayer("main");
        }
    }
    avtUI->View->graph(rowIndex)->setPen(QPen(Qt::white, 3));
    avtUI->View->graph(rowIndex)->setLayer("currentCurveLayer");
    avtUI->View->update();
    avtUI->View->replot();

    if (row >= 0 && row < pList.size())
    {
        SkyObject *p = pList.at(row);
        avtUI->raBox->show(p->ra());
        avtUI->decBox->show(p->dec());
        avtUI->nameBox->setText(avtUI->PlotList->currentItem()->text());
    }

    SkyObject *selectedObject = KStarsData::Instance()->objectNamed(avtUI->nameBox->text());
    const KStarsDateTime &ut  = KStarsData::Instance()->ut();
    if (selectedObject)
    {
        QTime rt = selectedObject->riseSetTime(ut, geo, true); //true = use rise time
        if (rt.isValid() == false)
        {
            avtUI->riseButton->setEnabled(false);
            avtUI->setButton->setEnabled(false);
        }
        else
        {
            avtUI->riseButton->setEnabled(true);
            avtUI->setButton->setEnabled(true);
        }
    }
}

void AltVsTime::onXRangeChanged(const QCPRange &range)
{
    QCPRange aux = avtUI->View->xAxis2->range();
    avtUI->View->xAxis->setRange(aux -= 18000);
    avtUI->View->xAxis2->setRange(range.bounded(61200, 147600));
    // if ZOOM is detected then remove the gold lines that indicate current position:
    if (avtUI->View->xAxis->range().size() != 86400)
    {
        // Refresh the background:
        background->setScaled(false);
        background->setScaled(true, Qt::IgnoreAspectRatio);
        background->setPixmap(*gradient);

        avtUI->View->update();
        avtUI->View->replot();
    }
}

void AltVsTime::onYRangeChanged(const QCPRange &range)
{
    int offset = 3;
    avtUI->View->yAxis->setRange(range.bounded(minAlt - offset, maxAlt + offset));
}

void AltVsTime::plotMousePress(QCPAbstractPlottable *abstractPlottable, int dataIndex, QMouseEvent *event)
{
    //Do we need this?
    Q_UNUSED(dataIndex);

    if (event->button() == Qt::RightButton)
    {
        QCPAbstractPlottable *plottable = abstractPlottable;
        if (plottable)
        {
            double x = avtUI->View->xAxis->pixelToCoord(event->localPos().x());
            double y = avtUI->View->yAxis->pixelToCoord(event->localPos().y());

            QCPGraph *graph = qobject_cast<QCPGraph *>(plottable);

            if (graph)
            {
                double yValue = y;
                double xValue = x;

                // Compute time value:
                QTime localTime(0, 0, 0, 0);
                QTime localSiderealTime(5, 0, 0, 0);

                localTime         = localTime.addSecs(int(xValue));
                localSiderealTime = localSiderealTime.addSecs(int(xValue));

                QToolTip::hideText();
                QToolTip::showText(event->globalPos(),
                                   i18n("<table>"
                                        "<tr>"
                                        "<th colspan=\"2\">%1</th>"
                                        "</tr>"
                                        "<tr>"
                                        "<td>LST:   </td>"
                                        "<td>%3</td>"
                                        "</tr>"
                                        "<tr>"
                                        "<td>LT:   </td>"
                                        "<td>%2</td>"
                                        "</tr>"
                                        "<tr>"
                                        "<td>Altitude:   </td>"
                                        "<td>%4</td>"
                                        "</tr>"
                                        "</table>",
                                        graph->name().isEmpty() ? "???" : graph->name(),
                                        localTime.toString(),
                                        localSiderealTime.toString(),
                                        QString::number(yValue, 'f', 2) + ' ' + QChar(176)),
                                   avtUI->View, avtUI->View->rect());
            }
        }
    }
}

//move input focus to the next logical widget
void AltVsTime::slotAdvanceFocus()
{
    if (sender()->objectName() == QString("nameBox"))
        avtUI->addButton->setFocus();
    if (sender()->objectName() == QString("raBox"))
        avtUI->decBox->setFocus();
    if (sender()->objectName() == QString("decbox"))
        avtUI->addButton->setFocus();
    if (sender()->objectName() == QString("longBox"))
        avtUI->latBox->setFocus();
    if (sender()->objectName() == QString("latBox"))
        avtUI->updateButton->setFocus();
}

void AltVsTime::slotClear()
{
    pList.clear();
    //Need to delete the pointers in deleteList
    while (!deleteList.isEmpty())
        delete deleteList.takeFirst();

    avtUI->PlotList->clear();
    avtUI->nameBox->clear();
    avtUI->raBox->clear();
    avtUI->decBox->clear();
    avtUI->epochName->clear();
    // remove all graphs from the plot:
    avtUI->View->clearGraphs();
    // we remove all the dots (rise/set/transit) from the chart
    // without removing the background image
    int indexItem = 0, noItems = avtUI->View->itemCount();
    QCPAbstractItem *item;
    QCPItemPixmap *background;
    // remove every item at a time:
    while (noItems > 1 && indexItem < noItems)
    {
        // test if the current item is the background:
        item       = avtUI->View->item(indexItem);
        background = qobject_cast<QCPItemPixmap *>(item);
        if (background)
            indexItem++;
        else
        {
            // if not, then remove this item:
            avtUI->View->removeItem(indexItem);
            noItems--;
        }
    }
    // update & replot the chart:
    avtUI->View->update();
    avtUI->View->replot();
}

void AltVsTime::slotClearBoxes()
{
    avtUI->nameBox->clear();
    avtUI->raBox->clear();
    avtUI->decBox->clear();
    avtUI->epochName->clear();
}

void AltVsTime::slotComputeAltitudeByTime()
{
    if (avtUI->PlotList->currentRow() < 0)
        return;

    SkyObject *selectedObject = pList.at(avtUI->PlotList->currentRow());
    if (selectedObject == nullptr)
    {
        if (avtUI->PlotList->currentItem())
            qCWarning(KSTARS) << "slotComputeAltitudeByTime: Unable to find" << avtUI->PlotList->currentItem()->text();
        else
        {
            qCWarning(KSTARS) << "slotComputeAltitudeByTime: Unable to find item";
        }
        return;
    }

    // Get Local Date & Time
    KStarsDateTime lt = KStarsDateTime(avtUI->DateWidget->date(), avtUI->timeSpin->time(), Qt::LocalTime);
    // Convert to UT
    KStarsDateTime ut = geo->LTtoUT(lt);
    // Get LST from GST
    CachingDms LST = geo->GSTtoLST(ut.gst());
    SkyObject *tempObject = selectedObject->clone();
    // Update coords
    KSNumbers num(ut.djd());
    tempObject->updateCoords(&num, true, geo->lat(), &LST);
    // Find Horizontal coordinates from LST & Latitude
    selectedObject->EquatorialToHorizontal(&LST, geo->lat());

    // Set altitude
    avtUI->altitudeBox->setText(selectedObject->altRefracted().toDMSString(true));

    delete (tempObject);
}

void AltVsTime::slotMarkRiseTime()
{
    if (avtUI->PlotList->currentRow() < 0)
        return;

    const KStarsDateTime &ut  = KStarsData::Instance()->ut();
    SkyObject *selectedObject = pList.at(avtUI->PlotList->currentRow());
    if (selectedObject == nullptr)
    {
        qCWarning(KSTARS) << "Mark Rise Time: Unable to find" << avtUI->PlotList->currentItem()->text();
        return;
    }

    QCPItemTracer *riseTimeTracer;
    // check if at least one graph exists in the plot
    if (avtUI->View->graphCount() > 0)
    {
        double time = 0;
        double hours, minutes;

        QCPGraph *selectedGraph = avtUI->View->graph(avtUI->PlotList->currentRow());

        QTime rt = selectedObject->riseSetTime(ut, geo, true); //true = use rise time
        // mark the Rise time with a solid red circle
        if (rt.isValid() && selectedGraph)
        {
            hours   = rt.hour();
            minutes = rt.minute();
            if (hours < 12)
                hours += 24;
            time           = hours * 3600 + minutes * 60;
            riseTimeTracer = new QCPItemTracer(avtUI->View);
            riseTimeTracer->setLayer("markersLayer");
            riseTimeTracer->setGraph(selectedGraph);
            riseTimeTracer->setInterpolating(true);
            riseTimeTracer->setStyle(QCPItemTracer::tsCircle);
            riseTimeTracer->setPen(QPen(Qt::red));
            riseTimeTracer->setBrush(Qt::red);
            riseTimeTracer->setSize(10);
            riseTimeTracer->setGraphKey(time);
            riseTimeTracer->setVisible(true);
            avtUI->View->update();
            avtUI->View->replot();
        }
    }
}

void AltVsTime::slotMarkSetTime()
{
    if (avtUI->PlotList->currentRow() < 0)
        return;

    const KStarsDateTime &ut  = KStarsData::Instance()->ut();
    SkyObject *selectedObject = pList.at(avtUI->PlotList->currentRow());
    if (selectedObject == nullptr)
    {
        qCWarning(KSTARS) << "Mark Set Time: Unable to find" << avtUI->PlotList->currentItem()->text();
        return;
    }
    QCPItemTracer *setTimeTracer;
    // check if at least one graph exists in the plot
    if (avtUI->View->graphCount() > 0)
    {
        double time = 0;
        double hours, minutes;

        QCPGraph *selectedGraph = avtUI->View->graph(avtUI->PlotList->currentRow());

        QTime rt = selectedObject->riseSetTime(ut, geo, true); //true = use rise time
        //If set time is before rise time, use set time for tomorrow
        QTime st = selectedObject->riseSetTime(ut, geo, false); //false = use set time
        if (st < rt)
            st = selectedObject->riseSetTime(ut.addDays(1), geo, false); //false = use set time
        // mark the Set time with a solid blue circle
        if (rt.isValid())
        {
            hours   = st.hour();
            minutes = st.minute();
            if (hours < 12)
                hours += 24;
            time          = hours * 3600 + minutes * 60;
            setTimeTracer = new QCPItemTracer(avtUI->View);
            setTimeTracer->setLayer("markersLayer");
            setTimeTracer->setGraph(selectedGraph);
            setTimeTracer->setInterpolating(true);
            setTimeTracer->setStyle(QCPItemTracer::tsCircle);
            setTimeTracer->setPen(QPen(Qt::blue));
            setTimeTracer->setBrush(Qt::blue);
            setTimeTracer->setSize(10);
            setTimeTracer->setGraphKey(time);
            setTimeTracer->setVisible(true);
            avtUI->View->update();
            avtUI->View->replot();
        }
    }
}

void AltVsTime::slotMarkTransitTime()
{
    if (avtUI->PlotList->currentRow() < 0)
        return;

    const KStarsDateTime &ut  = KStarsData::Instance()->ut();
    SkyObject *selectedObject = pList.at(avtUI->PlotList->currentRow());
    if (selectedObject == nullptr)
    {
        qCWarning(KSTARS) << "Mark Transit Time: Unable to find" << avtUI->PlotList->currentItem()->text();
        return;
    }
    QCPItemTracer *transitTimeTracer;
    // check if at least one graph exists in the plot
    if (avtUI->View->graphCount() > 0)
    {
        double time = 0;
        double hours, minutes;

        QCPGraph *selectedGraph = avtUI->View->graph(avtUI->PlotList->currentRow());

        QTime rt = selectedObject->riseSetTime(ut, geo, true); //true = use rise time
        //If transit time is before rise time, use transit time for tomorrow
        QTime tt = selectedObject->transitTime(ut, geo);

        if (tt < rt)
            tt = selectedObject->transitTime(ut.addDays(1), geo);
        // mark the Transit time with a solid green circle
        hours   = tt.hour();
        minutes = tt.minute();
        if (hours < 12)
            hours += 24;
        time              = hours * 3600 + minutes * 60;
        transitTimeTracer = new QCPItemTracer(avtUI->View);
        transitTimeTracer->setLayer("markersLayer");
        transitTimeTracer->setGraph(selectedGraph);
        transitTimeTracer->setInterpolating(true);
        transitTimeTracer->setStyle(QCPItemTracer::tsCircle);
        transitTimeTracer->setPen(QPen(Qt::green));
        transitTimeTracer->setBrush(Qt::green);
        transitTimeTracer->setSize(10);
        transitTimeTracer->setGraphKey(time);
        transitTimeTracer->setVisible(true);
        avtUI->View->update();
        avtUI->View->replot();
    }
}

void AltVsTime::mouseOverLine(QMouseEvent *event)
{
    double x                            = avtUI->View->xAxis->pixelToCoord(event->localPos().x());
    double y                            = avtUI->View->yAxis->pixelToCoord(event->localPos().y());
    QCPAbstractPlottable *abstractGraph = avtUI->View->plottableAt(event->pos(), false);
    QCPGraph *graph                     = qobject_cast<QCPGraph *>(abstractGraph);

    if (x > avtUI->View->xAxis->range().lower && x < avtUI->View->xAxis->range().upper)
        if (y > avtUI->View->yAxis->range().lower && y < avtUI->View->yAxis->range().upper)
        {
            if (graph)
            {
                double yValue = y;
                double xValue = x;

                // Compute time value:
                QTime localTime(0, 0, 0, 0);
                QTime localSiderealTime(5, 0, 0, 0);

                localTime         = localTime.addSecs(int(xValue));
                localSiderealTime = localSiderealTime.addSecs(int(xValue));

                QToolTip::hideText();
                QToolTip::showText(event->globalPos(),
                                   i18n("<table>"
                                        "<tr>"
                                        "<th colspan=\"2\">%1</th>"
                                        "</tr>"
                                        "<tr>"
                                        "<td>LST:   </td>"
                                        "<td>%3</td>"
                                        "</tr>"
                                        "<tr>"
                                        "<td>LT:   </td>"
                                        "<td>%2</td>"
                                        "</tr>"
                                        "<tr>"
                                        "<td>Altitude:   </td>"
                                        "<td>%4</td>"
                                        "</tr>"
                                        "</table>",
                                        graph->name().isEmpty() ? "???" : graph->name(),
                                        localTime.toString(), localSiderealTime.toString(),
                                        QString::number(yValue, 'f', 2) + ' ' + QChar(176)),
                                   avtUI->View, avtUI->View->rect());
            }
            else
                QToolTip::hideText();
        }

    avtUI->View->update();
    avtUI->View->replot();
}

void AltVsTime::slotUpdateDateLoc()
{
    KStarsData *data     = KStarsData::Instance();
    KStarsDateTime today = getDate();
    KSNumbers *num       = new KSNumbers(today.djd());
    KSNumbers *oldNum    = nullptr;
    CachingDms LST       = geo->GSTtoLST(today.gst());

    //First determine time of sunset and sunrise
    // computeSunRiseSetTimes();
    // Determine dawn/dusk time and min/max sun elevation
    // setDawnDusk();

    for (int i = 0; i < pList.count(); ++i)
    {
        SkyObject *o = pList.at(i);
        if (o)
        {
            //If the object is in the solar system, recompute its position for the given date
            if (o->isSolarSystem())
            {
                oldNum = new KSNumbers(data->ut().djd());
                o->updateCoords(num, true, geo->lat(), &LST, true);
            }

            //precess coords to target epoch
            o->updateCoordsNow(num);

            //update pList entry
            pList.replace(i, o);

            // We are creating a new data set (time, altitude) for the new date:
            QVector<double> time_dataSet, altitude_dataSet;
            double point_altitudeValue, point_timeValue;
            // compute the new graph values:
            // time range: 24h
            int offset = 3;
            for (double h = -12.0, i = 0; h <= 12.0; h += 0.25, i++)
            {
                point_altitudeValue = findAltitude(o, h);
                altitude_dataSet.push_back(point_altitudeValue);
                if (point_altitudeValue > maxAlt)
                    maxAlt = point_altitudeValue;
                if (point_altitudeValue < minAlt)
                    minAlt = point_altitudeValue;
                point_timeValue = i * 900 + 43200;
                time_dataSet.push_back(point_timeValue);
            }

            // Replace graph data set:
            avtUI->View->graph(i)->setData(time_dataSet, altitude_dataSet);

            // Go into initial state: without Zoom/Pan
            avtUI->View->xAxis->setRange(43200, 129600);
            avtUI->View->xAxis2->setRange(61200, 147600);

            // Center the altitude axis in 0 value:
            if (abs(minAlt) > maxAlt)
                maxAlt = abs(minAlt);
            else
                minAlt = -maxAlt;
            avtUI->View->yAxis->setRange(minAlt - offset, maxAlt + offset);

            // Update background coordinates:
            background->topLeft->setCoords(avtUI->View->xAxis->range().lower, avtUI->View->yAxis->range().upper);
            background->bottomRight->setCoords(avtUI->View->xAxis->range().upper, avtUI->View->yAxis->range().lower);

            // Redraw the plot:
            avtUI->View->replot();

            //restore original position
            if (o->isSolarSystem())
            {
                o->updateCoords(oldNum, true, data->geo()->lat(), data->lst());
                delete oldNum;
                oldNum = nullptr;
            }
            o->EquatorialToHorizontal(data->lst(), data->geo()->lat());
        }
        else //assume unfound object is a custom object
        {
            pList.at(i)->updateCoordsNow(num); //precess to desired epoch

            // We are creating a new data set (time, altitude) for the new date:
            QVector<double> time_dataSet, altitude_dataSet;
            double point_altitudeValue, point_timeValue;
            // compute the new graph values:
            // time range: 24h
            int offset = 3;
            for (double h = -12.0, i = 0; h <= 12.0; h += 0.25, i++)
            {
                point_altitudeValue = findAltitude(pList.at(i), h);
                altitude_dataSet.push_back(point_altitudeValue);
                if (point_altitudeValue > maxAlt)
                    maxAlt = point_altitudeValue;
                if (point_altitudeValue < minAlt)
                    minAlt = point_altitudeValue;
                point_timeValue = i * 900 + 43200;
                time_dataSet.push_back(point_timeValue);
            }

            // Replace graph data set:
            avtUI->View->graph(i)->setData(time_dataSet, altitude_dataSet);

            // Go into initial state: without Zoom/Pan
            avtUI->View->xAxis->setRange(43200, 129600);
            avtUI->View->xAxis2->setRange(61200, 147600);

            // Center the altitude axis in 0 value:
            if (abs(minAlt) > maxAlt)
                maxAlt = abs(minAlt);
            else
                minAlt = -maxAlt;
            avtUI->View->yAxis->setRange(minAlt - offset, maxAlt + offset);

            // Update background coordinates:
            background->topLeft->setCoords(avtUI->View->xAxis->range().lower, avtUI->View->yAxis->range().upper);
            background->bottomRight->setCoords(avtUI->View->xAxis->range().upper, avtUI->View->yAxis->range().lower);

            // Redraw the plot:
            avtUI->View->replot();
        }
    }

    if (getDate().time().hour() > 12)
        DayOffset = 1;
    else
        DayOffset = 0;

    //setLSTLimits();
    slotHighlight(avtUI->PlotList->currentRow());
    avtUI->View->update();

    delete num;
}

void AltVsTime::slotChooseCity()
{
    QPointer<LocationDialog> ld = new LocationDialog(this);
    if (ld->exec() == QDialog::Accepted)
    {
        GeoLocation *newGeo = ld->selectedCity();
        if (newGeo)
        {
            geo = newGeo;
            avtUI->latBox->show(geo->lat());
            avtUI->longBox->show(geo->lng());
        }
    }
    delete ld;
}

void AltVsTime::showCurrentDate()
{
    KStarsDateTime dt = KStarsDateTime::currentDateTime();
    if (dt.time() > QTime(12, 0, 0))
        dt = dt.addDays(1);
    avtUI->DateWidget->setDate(dt.date());
}

void AltVsTime::drawGradient()
{
    // Things needed for Gradient:
    KStarsDateTime dtt  = KStarsDateTime::currentDateTime();
    GeoLocation *geoLoc = KStarsData::Instance()->geo();
    QDateTime midnight  = QDateTime(dtt.date(), QTime());
    KStarsDateTime const utt  = geoLoc->LTtoUT(KStarsDateTime(midnight));

    // Variables needed for Gradient:
    double SunRise, SunSet, Dawn, Dusk, SunMinAlt, SunMaxAlt;
    double MoonRise, MoonSet, MoonIllum;

    KSAlmanac ksal(utt, geoLoc);

    // Get the values:
    SunRise   = ksal.getSunRise();
    SunSet    = ksal.getSunSet();
    SunMaxAlt = ksal.getSunMaxAlt();
    SunMinAlt = ksal.getSunMinAlt();
    MoonRise  = ksal.getMoonRise();
    MoonSet   = ksal.getMoonSet();
    MoonIllum = ksal.getMoonIllum();
    Dawn      = ksal.getDawnAstronomicalTwilight();
    Dusk      = ksal.getDuskAstronomicalTwilight();

    gradient = new QPixmap(avtUI->View->rect().width(), avtUI->View->rect().height());

    QPainter p;

    p.begin(gradient);
    KPlotWidget *kPW = new KPlotWidget;
    p.setRenderHint(QPainter::Antialiasing, kPW->antialiasing());
    p.fillRect(gradient->rect(), kPW->backgroundColor());

    p.setClipRect(gradient->rect());
    p.setClipping(true);

    int pW = gradient->rect().width();
    int pH = gradient->rect().height();

    QColor SkyColor(0, 100, 200);
    //    TODO
    //    if( Options::darkAppColors() )
    //        SkyColor = QColor( 200, 0, 0 ); // use something red, visible through a red filter

    // Draw gradient representing lunar interference in the sky
    if (MoonIllum > 0.01) // do this only if Moon illumination is reasonable so it's important
    {
        int moonrise = int(pW * (0.5 + MoonRise));
        int moonset  = int(pW * (MoonSet - 0.5));
        if (moonset < 0)
            moonset += pW;
        if (moonrise > pW)
            moonrise -= pW;
        int moonalpha = int(10 + MoonIllum * 130);
        int fadewidth =
            pW *
            0.01; // pW * fraction of day to fade the moon brightness over (0.01 corresponds to roughly 15 minutes, 0.007 to 10 minutes), both before and after actual set.
        QColor MoonColor(255, 255, 255, moonalpha);

        if (moonset < moonrise)
        {
            QLinearGradient grad =
                QLinearGradient(QPointF(moonset - fadewidth, 0.0), QPointF(moonset + fadewidth, 0.0));
            grad.setColorAt(0, MoonColor);
            grad.setColorAt(1, Qt::transparent);
            p.fillRect(QRectF(0.0, 0.0, moonset + fadewidth, pH),
                       grad); // gradient should be padded until moonset - fadewidth (see QLinearGradient docs)
            grad.setStart(QPointF(moonrise + fadewidth, 0.0));
            grad.setFinalStop(QPointF(moonrise - fadewidth, 0.0));
            p.fillRect(QRectF(moonrise - fadewidth, 0.0, pW - moonrise + fadewidth, pH), grad);
        }
        else
        {
            qreal opacity = p.opacity();
            p.setOpacity(opacity / 4);
            p.fillRect(QRectF(moonrise + fadewidth, 0.0, moonset - moonrise - 2 * fadewidth, pH), MoonColor);
            QLinearGradient grad =
                QLinearGradient(QPointF(moonrise + fadewidth, 0.0), QPointF(moonrise - fadewidth, 0.0));
            grad.setColorAt(0, MoonColor);
            grad.setColorAt(1, Qt::transparent);
            p.fillRect(QRectF(0.0, 0.0, moonrise + fadewidth, pH), grad);
            grad.setStart(QPointF(moonset - fadewidth, 0.0));
            grad.setFinalStop(QPointF(moonset + fadewidth, 0.0));
            p.fillRect(QRectF(moonset - fadewidth, 0.0, pW - moonset, pH), grad);
            p.setOpacity(opacity);
        }
    }

    //draw daytime sky if the Sun rises for the current date/location
    if (SunMaxAlt > -18.0)
    {
        //Display centered on midnight, so need to modulate dawn/dusk by 0.5
        int rise = int(pW * (0.5 + SunRise));
        int set  = int(pW * (SunSet - 0.5));
        int da   = int(pW * (0.5 + Dawn));
        int du   = int(pW * (Dusk - 0.5));

        if (SunMinAlt > 0.0)
        {
            // The sun never set and the sky is always blue
            p.fillRect(rect(), SkyColor);
        }
        else if (SunMaxAlt < 0.0 && SunMinAlt < -18.0)
        {
            // The sun never rise but the sky is not completely dark
            QLinearGradient grad = QLinearGradient(QPointF(0.0, 0.0), QPointF(du, 0.0));

            QColor gradStartColor = SkyColor;
            gradStartColor.setAlpha((1 - (SunMaxAlt / -18.0)) * 255);

            grad.setColorAt(0, gradStartColor);
            grad.setColorAt(1, Qt::transparent);
            p.fillRect(QRectF(0.0, 0.0, du, pH), grad);
            grad.setStart(QPointF(pW, 0.0));
            grad.setFinalStop(QPointF(da, 0.0));
            p.fillRect(QRectF(da, 0.0, pW, pH), grad);
        }
        else if (SunMaxAlt < 0.0 && SunMinAlt > -18.0)
        {
            // The sun never rise but the sky is NEVER completely dark
            QLinearGradient grad = QLinearGradient(QPointF(0.0, 0.0), QPointF(pW, 0.0));

            QColor gradStartEndColor = SkyColor;
            gradStartEndColor.setAlpha((1 - (SunMaxAlt / -18.0)) * 255);
            QColor gradMidColor = SkyColor;
            gradMidColor.setAlpha((1 - (SunMinAlt / -18.0)) * 255);

            grad.setColorAt(0, gradStartEndColor);
            grad.setColorAt(0.5, gradMidColor);
            grad.setColorAt(1, gradStartEndColor);
            p.fillRect(QRectF(0.0, 0.0, pW, pH), grad);
        }
        else if (Dawn < 0.0)
        {
            // The sun sets and rises but the sky is never completely dark
            p.fillRect(0, 0, set, int(0.5 * pH), SkyColor);
            p.fillRect(rise, 0, pW, int(0.5 * pH), SkyColor);

            QLinearGradient grad = QLinearGradient(QPointF(set, 0.0), QPointF(rise, 0.0));

            QColor gradMidColor = SkyColor;
            gradMidColor.setAlpha((1 - (SunMinAlt / -18.0)) * 255);

            grad.setColorAt(0, SkyColor);
            grad.setColorAt(0.5, gradMidColor);
            grad.setColorAt(1, SkyColor);
            p.fillRect(QRectF(set, 0.0, rise - set, pH), grad);
        }
        else
        {
            p.fillRect(0, 0, set, pH, SkyColor);
            p.fillRect(rise, 0, pW, pH, SkyColor);

            QLinearGradient grad = QLinearGradient(QPointF(set, 0.0), QPointF(du, 0.0));
            grad.setColorAt(0, SkyColor);
            grad.setColorAt(
                1,
                Qt::transparent); // FIXME?: The sky appears black well before the actual end of twilight if the gradient is too slow (eg: latitudes above arctic circle)
            p.fillRect(QRectF(set, 0.0, du - set, pH), grad);

            grad.setStart(QPointF(rise, 0.0));
            grad.setFinalStop(QPointF(da, 0.0));
            p.fillRect(QRectF(da, 0.0, rise - da, pH), grad);
        }
    }

    p.fillRect(0, int(0.5 * pH), pW, int(0.5 * pH), KStarsData::Instance()->colorScheme()->colorNamed("HorzColor"));

    p.setClipping(false);

    // Add vertical line indicating "now"
    // Convert the current system clock time to the TZ corresponding to geo
    QTime t = geoLoc->UTtoLT(KStarsDateTime::currentDateTimeUtc()).time();
    double x = 12.0 + t.hour() + t.minute() / 60.0 + t.second() / 3600.0;

    while (x > 24.0)
        x -= 24.0;

    // Convert to screen pixel coords
    int ix = int(x * pW / 24.0);

    p.setPen(QPen(QBrush("white"), 2.0, Qt::DotLine));
    p.drawLine(ix, 0, ix, pH);

    QFont largeFont = p.font();

    largeFont.setPointSize(largeFont.pointSize() + 1);
    // Label this vertical line with the current time
    p.save();
    p.setFont(largeFont);
    p.translate(ix + 15, pH - 20);
    p.rotate(-90);
    // Short format necessary to avoid false time-zone labeling
    p.drawText(0, 0, QLocale().toString(t, QLocale::ShortFormat));
    p.restore();
    p.end();
}

KStarsDateTime AltVsTime::getDate()
{
    //convert midnight local time to UT:
    QDateTime lt(avtUI->DateWidget->date(), QTime());
    return geo->LTtoUT(KStarsDateTime(lt));
}

double AltVsTime::getEpoch(const QString &eName)
{
    //If Epoch field not a double, assume J2000
    bool ok;
    double epoch = eName.toDouble(&ok);
    if (!ok)
    {
        qCWarning(KSTARS) << "Invalid Epoch.  Assuming 2000.0.";
        return 2000.0;
    }
    return epoch;
}

void AltVsTime::slotPrint()
{
    QPainter p;            // Our painter object
    QPrinter printer;      // Our printer object
    QString str_legend;    // Text legend
    int text_height = 200; // Height of legend text zone in points
    QSize plot_size;       // Initial plot widget size
    QFont plot_font;       // Initial plot widget font
    int plot_font_size;    // Initial plot widget font size

    // Set printer resolution to 300 dpi
    printer.setResolution(300);

    // Open print dialog
    //QPointer<QPrintDialog> dialog( KdePrint::createPrintDialog( &printer, this ) );
    //QPointer<QPrintDialog> dialog( &printer, this );
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(i18nc("@title:window", "Print elevation vs time plot"));
    if (dialog.exec() == QDialog::Accepted)
    {
        // Change mouse cursor
        QApplication::setOverrideCursor(Qt::WaitCursor);

        // Save plot widget font
        plot_font = avtUI->View->font();
        // Save plot widget font size
        plot_font_size = plot_font.pointSize();
        // Save calendar widget size
        plot_size = avtUI->View->size();

        // Set text legend
        str_legend = i18n("Elevation vs. Time Plot");
        str_legend += '\n';
        str_legend += geo->fullName();
        str_legend += " - ";
        str_legend += avtUI->DateWidget->date().toString("dd/MM/yyyy");

        // Create a rectangle for legend text zone
        QRect text_rect(0, 0, printer.width(), text_height);

        // Increase plot widget font size so it looks good in 300 dpi
        plot_font.setPointSize(plot_font_size * 2.5);
        avtUI->View->setFont(plot_font);
        // Increase plot widget size to fit the entire page
        avtUI->View->resize(printer.width(), printer.height() - text_height);

        // Create a pixmap and render plot widget into it
        QPixmap pixmap(avtUI->View->size());
        avtUI->View->render(&pixmap);

        // Begin painting on printer
        p.begin(&printer);
        // Draw legend
        p.drawText(text_rect, Qt::AlignLeft, str_legend);
        // Draw plot widget
        p.drawPixmap(0, text_height, pixmap);
        // Ending painting
        p.end();

        // Restore plot widget font size
        plot_font.setPointSize(plot_font_size);
        avtUI->View->setFont(plot_font);
        // Restore calendar widget size
        avtUI->View->resize(plot_size);

        // Restore mouse cursor
        QApplication::restoreOverrideCursor();
    }
    //delete dialog;
}

QString AltVsTime::getObjectName(const SkyObject *o, bool translated)
{
    QString finalObjectName;
    if (o->name() == "star")
    {
        StarObject *s = (StarObject *)o;

        // JM: Enable HD Index stars to be added to the observing list.
        if (s->getHDIndex() != 0)
            finalObjectName = QString("HD %1").arg(QString::number(s->getHDIndex()));
    }
    else
        finalObjectName = translated ? o->translatedName() : o->name();

    return finalObjectName;
}
