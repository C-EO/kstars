/*
    SPDX-FileCopyrightText: 2026 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <basedevice.h>
#include <QtDBus/qdbusmetatype.h>

#include "indipac.h"
#include "pacadaptor.h"
#include "ksnotification.h"

namespace ISD
{

const QList<KLocalizedString> PAC::pacStates = { ki18n("Idle"), ki18n("Correcting"), ki18n("Corrected"), ki18n("Error") };

PAC::PAC(GenericDevice *parent): ConcreteDevice(parent)
{
    qRegisterMetaType<ISD::PAC::Status>("ISD::PAC::Status");
    qDBusRegisterMetaType<ISD::PAC::Status>();

    new PACAdaptor(this);
    m_DBusObjectPath = QString("/KStars/INDI/PAC/%1").arg(getID());
    QDBusConnection::sessionBus().registerObject(m_DBusObjectPath, this);
}

void PAC::processSwitch(INDI::Property prop)
{
    auto svp = prop.getSwitch();
    if (svp->isNameMatch("ALIGNMENT_CORRECTION"))
    {
        Status currentStatus = PAC_IDLE;

        switch (svp->getState())
        {
            case IPS_IDLE:
                currentStatus = PAC_IDLE;
                break;

            case IPS_OK:
                currentStatus = PAC_CORRECTED;
                KSNotification::event(QLatin1String("IndiServerMessage"), i18n("Polar alignment correction completed"),
                                      KSNotification::Observatory, KSNotification::Info);
                break;

            case IPS_BUSY:
                currentStatus = PAC_CORRECTING;
                KSNotification::event(QLatin1String("IndiServerMessage"), i18n("Polar alignment correction in progress"),
                                      KSNotification::Observatory, KSNotification::Info);
                break;

            case IPS_ALERT:
                currentStatus = PAC_ERROR;
                KSNotification::event(QLatin1String("IndiServerMessage"), i18n("Polar alignment correction error"),
                                      KSNotification::Observatory, KSNotification::Alert);
                break;
        }

        if (currentStatus != m_Status)
        {
            m_Status = currentStatus;
            emit newStatus(m_Status);
        }
    }
}

void PAC::processNumber(INDI::Property prop)
{
    Q_UNUSED(prop)
    // ALIGNMENT_CORRECTION_ERROR and PAC_MANUAL_ADJUSTMENT are handled transparently;
    // the driver updates the values internally. No additional KStars-side status change needed.
}

void PAC::processLight(INDI::Property prop)
{
    auto lvp = prop.getLight();
    if (lvp->isNameMatch("ALIGNMENT_CORRECTION_STATUS"))
    {
        if (lvp->count() == 0)
            return;

        IPState lightState = lvp->at(0)->getState();
        Status currentStatus = m_Status;

        switch (lightState)
        {
            case IPS_IDLE:
                currentStatus = PAC_IDLE;
                break;
            case IPS_OK:
                currentStatus = PAC_CORRECTED;
                break;
            case IPS_BUSY:
                currentStatus = PAC_CORRECTING;
                break;
            case IPS_ALERT:
                currentStatus = PAC_ERROR;
                break;
        }

        if (currentStatus != m_Status)
        {
            m_Status = currentStatus;
            emit newStatus(m_Status);
        }
    }
}

bool PAC::startCorrection(double azError, double altError)
{
    // Set the error values first
    auto errorNP = getNumber("ALIGNMENT_CORRECTION_ERROR");
    if (!errorNP)
        return false;

    auto azNP = errorNP.findWidgetByName("AZ_ERROR");
    auto altNP = errorNP.findWidgetByName("ALT_ERROR");
    if (!azNP || !altNP)
        return false;

    azNP->setValue(azError);
    altNP->setValue(altError);
    sendNewProperty(errorNP);

    // Now send the CORRECT switch
    auto correctionSP = getSwitch("ALIGNMENT_CORRECTION");
    if (!correctionSP)
        return false;

    auto correctSW = correctionSP.findWidgetByName("CORRECT");
    if (!correctSW)
        return false;

    correctionSP.reset();
    correctSW->setState(ISS_ON);
    sendNewProperty(correctionSP);

    return true;
}

bool PAC::abortCorrection()
{
    auto correctionSP = getSwitch("ALIGNMENT_CORRECTION");
    if (!correctionSP)
        return false;

    auto abortSW = correctionSP.findWidgetByName("ABORT");
    if (!abortSW)
        return false;

    correctionSP.reset();
    abortSW->setState(ISS_ON);
    sendNewProperty(correctionSP);

    return true;
}

bool PAC::moveAZ(double degrees)
{
    auto manualNP = getNumber("PAC_MANUAL_ADJUSTMENT");
    if (!manualNP)
        return false;

    auto azNP = manualNP.findWidgetByName("MANUAL_AZ_STEP");
    if (!azNP)
        return false;

    azNP->setValue(degrees);
    sendNewProperty(manualNP);

    return true;
}

bool PAC::moveALT(double degrees)
{
    auto manualNP = getNumber("PAC_MANUAL_ADJUSTMENT");
    if (!manualNP)
        return false;

    auto altNP = manualNP.findWidgetByName("MANUAL_ALT_STEP");
    if (!altNP)
        return false;

    altNP->setValue(degrees);
    sendNewProperty(manualNP);

    return true;
}

const QString PAC::getStatusString(PAC::Status status, bool translated)
{
    return translated ? pacStates[status].toString() : pacStates[status].untranslatedText();
}

}

QDBusArgument &operator<<(QDBusArgument &argument, const ISD::PAC::Status &source)
{
    argument.beginStructure();
    argument << static_cast<int>(source);
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ISD::PAC::Status &dest)
{
    int a;
    argument.beginStructure();
    argument >> a;
    argument.endStructure();
    dest = static_cast<ISD::PAC::Status>(a);
    return argument;
}
