/*
    SPDX-FileCopyrightText: 2020 Hy Murveit <hy@murveit.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "calibration.h"
#include "Options.h"
#include "ekos_guide_debug.h"
#include "ekos/auxiliary/rotatorutils.h"
#include "ksutils.h"
#include <QDateTime>
#include "indi/indimount.h"

#define CAL_VERSION     "Cal v1.1"
#define CAL_VERSION_1_0 "Cal v1.0"

Calibration::Calibration()
{
    ROT_Z = GuiderUtils::Matrix(0);
}

// Set angle
void Calibration::setAngle(double rotationAngle)
{
    m_Current.Angle = rotationAngle;
    // Matrix is set a priori to NEGATIVE angle, because we want a CCW rotation in the
    // right hand system of the sensor coordinate system.
    // Rotation is used in guidestars::computeStarDrift() and guidestars::getDrift()
    ROT_Z = GuiderUtils::RotateZ(-M_PI * m_Current.Angle / 180.0);
}

void Calibration::setParameters(double ccd_pix_width, double ccd_pix_height,
                                double focalLengthMm,
                                int binX, int binY,
                                ISD::Mount::PierSide currentPierSide,
                                const dms &mountRa, const dms &mountDec)
{
    m_Current.FocalLength = focalLengthMm;
    m_Current.CCDPixelWidth = ccd_pix_width;
    m_Current.CCDPixelHeight = ccd_pix_height;
    m_Current.MountRA = mountRa;
    m_Current.MountDEC = mountDec;
    m_Current.SubBinX = binX;
    m_Current.SubBinY = binY;
    m_Current.PierSide = currentPierSide;
}

void Calibration::setBinningUsed(int x, int y)
{
    m_Current.SubBinX = x;
    m_Current.SubBinY = y;
}

void Calibration::setRaPulseMsPerArcsecond(double rate)
{
    m_Current.PulseRA = rate;
}

void Calibration::setDecPulseMsPerArcsecond(double rate)
{
    m_Current.PulseDEC = rate;
}

GuiderUtils::Vector Calibration::convertToArcseconds(const GuiderUtils::Vector &input) const
{
    GuiderUtils::Vector arcseconds;
    arcseconds.x = input.x * xArcsecondsPerPixel();
    arcseconds.y = input.y * yArcsecondsPerPixel();
    return arcseconds;
}

GuiderUtils::Vector Calibration::convertToPixels(const GuiderUtils::Vector &input) const
{
    GuiderUtils::Vector arcseconds;
    arcseconds.x = input.x / xArcsecondsPerPixel();
    arcseconds.y = input.y / yArcsecondsPerPixel();
    return arcseconds;
}

void Calibration::convertToPixels(double xArcseconds, double yArcseconds,
                                  double *xPixel, double *yPixel) const
{
    *xPixel = xArcseconds / xArcsecondsPerPixel();
    *yPixel = yArcseconds / yArcsecondsPerPixel();
}

GuiderUtils::Vector Calibration::rotateToRaDec(const GuiderUtils::Vector &input) const
{
    GuiderUtils::Vector in;
    in.x = input.x;
    in.y = input.y;
    return (in * ROT_Z);
}

void Calibration::rotateToRaDec(double dx, double dy,
                                double *ra, double *dec) const
{
    GuiderUtils::Vector input;
    input.x = dx;
    input.y = dy;
    GuiderUtils::Vector out = rotateToRaDec(input);
    *ra = out.x;
    *dec = out.y;
}

double Calibration::binFactor() const
{
    return static_cast<double>(m_Current.SubBinX) / static_cast<double>(m_Calibration.SubBinX);
}

double Calibration::inverseBinFactor() const
{
    return 1.0 / binFactor();
}

double Calibration::xArcsecondsPerPixel() const
{
    // arcs = 3600*180/pi * (pix*ccd_pix_sz) / focal_len
    return binFactor() * (206264.806 * m_Current.CCDPixelWidth * m_Calibration.SubBinX) / m_Current.FocalLength;
}

double Calibration::yArcsecondsPerPixel() const
{
    return binFactor() * (206264.806 * m_Current.CCDPixelHeight * m_Calibration.SubBinY) / m_Current.FocalLength;
}

double Calibration::xPixelsPerArcsecond() const
{
    return inverseBinFactor() * (m_Current.FocalLength / (206264.806 * m_Current.CCDPixelWidth * m_Calibration.SubBinX));
}

double Calibration::yPixelsPerArcsecond() const
{
    return inverseBinFactor() * (m_Current.FocalLength / (206264.806 * m_Current.CCDPixelHeight * m_Calibration.SubBinY));
}

double Calibration::raPulseMillisecondsPerArcsecond() const
{
    return m_Current.PulseRA;
}

double Calibration::decPulseMillisecondsPerArcsecond() const
{
    return m_Current.PulseDEC;
}

double Calibration::calculateRotation(double x, double y)
{
    double phi;

    //if( (!GuiderUtils::Vector(delta_x, delta_y, 0)) < 2.5 )
    // JM 2015-12-10: Lower threshold to 1 pixel
    if ((!GuiderUtils::Vector(x, y, 0)) < 1)
        return -1;

    // 90 or 270 degrees
    if (fabs(x) < fabs(y) / 1000000.0)
    {
        phi = y > 0 ? 90.0 : 270;
    }
    else
    {
        phi = 180.0 / M_PI * atan2(y, x);
        if (phi < 0)
            phi += 360.0;
    }

    return phi;
}

bool Calibration::calculate1D(double start_x, double start_y,
                              double end_x, double end_y, int RATotalPulse)
{
    return calculate1D(end_x - start_x, end_y - start_y, RATotalPulse);
}

bool Calibration::calculate1D(double dx, double dy, int RATotalPulse)
{
    const double arcSecondsX = dx * xArcsecondsPerPixel();
    const double arcSecondsY = dy * yArcsecondsPerPixel();
    const double arcSeconds = std::hypot(arcSecondsX, arcSecondsY);
    if (arcSeconds < .1 || RATotalPulse <= 0)
    {
        qCDebug(KSTARS_EKOS_GUIDE)
                << QString("Bad input to calculate1D: ra %1 %2 total pulse %3")
                .arg(dx).arg(dy).arg(RATotalPulse);
        return false;
    }

    const double phi = calculateRotation(arcSecondsX, arcSecondsY);
    if (phi < 0)
        return false;

    setAngle(phi);
    m_Current.Angle = phi;
    m_Current.AngleRA = phi;
    m_Current.AngleDEC = -1;
    m_Current.DecSwap = false;

    if (RATotalPulse > 0)
        setRaPulseMsPerArcsecond(RATotalPulse / arcSeconds);

    if (raPulseMillisecondsPerArcsecond() > 10000)
    {
        qCDebug(KSTARS_EKOS_GUIDE)
                << "Calibration computed unreasonable pulse-milliseconds-per-arcsecond: "
                << raPulseMillisecondsPerArcsecond() << " & " << decPulseMillisecondsPerArcsecond();
    }
    m_initialized = true;
    return true;
}

bool Calibration::calculate2D(
    double start_ra_x, double start_ra_y, double end_ra_x, double end_ra_y,
    double start_dec_x, double start_dec_y, double end_dec_x, double end_dec_y,
    bool *invertedDECAxis, bool *reflectedImage, int RATotalPulse, int DETotalPulse)
{
    return calculate2D((end_ra_x - start_ra_x), (end_ra_y - start_ra_y),
                       (end_dec_x - start_dec_x), (end_dec_y - start_dec_y),
                       invertedDECAxis, reflectedImage, RATotalPulse, DETotalPulse);
}
bool Calibration::calculate2D(double ra_dx, double ra_dy, double dec_dx, double dec_dy,
                              bool *invertedDECAxis, bool *reflectedImage, int RATotalPulse, int DETotalPulse)
{
    const double raArcsecondsdX = ra_dx * xArcsecondsPerPixel();
    const double raArcsecondsdY = ra_dy * yArcsecondsPerPixel();
    const double decArcsecondsdX = dec_dx * xArcsecondsPerPixel();
    const double decArcsecondsdY = dec_dy * yArcsecondsPerPixel();
    const double raArcseconds = std::hypot(raArcsecondsdX, raArcsecondsdY);
    const double decArcseconds = std::hypot(decArcsecondsdX, decArcsecondsdY);
    if (raArcseconds < .1 || decArcseconds < .1 || RATotalPulse <= 0 || DETotalPulse <= 0)
    {
        qCDebug(KSTARS_EKOS_GUIDE)
                << QString("Bad input to calculate2D: ra %1 %2 dec %3 %4 total pulses %5 %6")
                .arg(ra_dx).arg(ra_dy).arg(dec_dx).arg(dec_dy).arg(RATotalPulse).arg(DETotalPulse);
        return false;
    }

    double phi_ra  = 0; // angle calculated by GUIDE_RA drift
    double phi_dec = 0; // angle calculated by GUIDE_DEC drift
    double phi     = 0;

    GuiderUtils::Vector ra_vect  = GuiderUtils::Normalize(GuiderUtils::Vector(raArcsecondsdX, raArcsecondsdY, 0));
    GuiderUtils::Vector dec_vect = GuiderUtils::Normalize(GuiderUtils::Vector(decArcsecondsdX, decArcsecondsdY, 0));

    // The following rotation senses are meant in respect to the right hand system of the
    // sensor coordinate system.
    GuiderUtils::Vector dec_vect_rotated_CCW = dec_vect * GuiderUtils::RotateZ(M_PI / 2);
    GuiderUtils::Vector dec_vect_rotated_CW = dec_vect * GuiderUtils::RotateZ(-M_PI / 2);

    double scalar_product_CCW = dec_vect_rotated_CCW & ra_vect;
    double scalar_product_CW = dec_vect_rotated_CW & ra_vect;

    bool ra_dec_is_CW_system = scalar_product_CCW > scalar_product_CW ? true : false;

    phi_ra = calculateRotation(raArcsecondsdX, raArcsecondsdY);
    if (phi_ra < 0)
        return false;

    phi_dec = calculateRotation(decArcsecondsdX, decArcsecondsdY);
    if (phi_dec < 0)
        return false;

    // Store the calibration angles.
    m_Current.AngleRA = phi_ra;
    m_Current.AngleDEC = phi_dec;

    if (ra_dec_is_CW_system)
        phi_dec += 90;
    else // ra-dec is standard CCW system
        phi_dec -= 90;

    if (phi_dec > 360)
        phi_dec -= 360.0;
    if (phi_dec < 0)
        phi_dec += 360.0;

    if (fabs(phi_dec - phi_ra) > 180)
    {
        if (phi_ra > phi_dec)
            phi_ra -= 360;
        else
            phi_dec -= 360;
    }

    // average angles
    phi = (phi_ra + phi_dec) / 2;
    if (phi < 0)
        phi += 360.0;

    // setAngle sets the angle we'll use when guiding.
    // [m_Current.Angle] is the saved angle to be stored.
    // They're the same now, but if we flip pier sides, angle may change.
    setAngle(phi);
    m_Current.Angle = phi;

    // check inverted DEC
    if (ra_dec_is_CW_system) // If a CW-system is present ...
    {
        if (RotatorUtils::Instance()->getGuideImageParity() == FITSImage::NEGATIVE)
            // .. and the image has negative (normal) parity then
            // the mount does not invert DEC (after pier side change):
            // DecSwap is set as to invert DEC pulses for correct guiding
            *invertedDECAxis = m_Current.DecSwap = true;
        else if (RotatorUtils::Instance()->getGuideImageParity() == FITSImage::POSITIVE)
            // ... and the image has positiv parity the guide camera returns
            // a vertically reflected image (typically with OAG/ONAG)
            *reflectedImage = true;
        else
            // In case of problems with RotatorUtils or for testcalibrationprocess.cpp
            m_Current.DecSwap = true;
    }

    if (RATotalPulse > 0)
        setRaPulseMsPerArcsecond(RATotalPulse / raArcseconds);

    if (DETotalPulse > 0)
        setDecPulseMsPerArcsecond(DETotalPulse / decArcseconds);

    // Check for unreasonable values.
    if (raPulseMillisecondsPerArcsecond() > 10000 || decPulseMillisecondsPerArcsecond() > 10000)
    {
        qCDebug(KSTARS_EKOS_GUIDE)
                << "Calibration computed unreasonable pulse-milliseconds-per-arcsecond: "
                << raPulseMillisecondsPerArcsecond() << " & " << decPulseMillisecondsPerArcsecond();
        return false;
    }

    qCDebug(KSTARS_EKOS_GUIDE) << QString("Set RA ms/as = %1ms / %2as = %3. DEC: %4ms / %5px = %6.")
                               .arg(RATotalPulse).arg(raArcseconds).arg(raPulseMillisecondsPerArcsecond())
                               .arg(DETotalPulse).arg(decArcseconds).arg(decPulseMillisecondsPerArcsecond());
    m_initialized = true;
    return true;
}

void Calibration::computeDrift(const GuiderUtils::Vector &detection, const GuiderUtils::Vector &reference,
                               double *raDrift, double *decDrift) const
{
    GuiderUtils::Vector drift = detection - reference;
    drift = rotateToRaDec(drift);
    *raDrift   = drift.x;
    *decDrift = drift.y;
}

void Calibration::setDeclinationSwapEnabled(bool value)
{
    m_Current.DecSwap = value;
    qCDebug(KSTARS_EKOS_GUIDE) << QString("DECSwap: Set to %1 (%2 in calibration)")
                               .arg(m_Current.DecSwap ? "On" : "Off")
                               .arg(m_Calibration.DecSwap ? "On" : "Off");
}

namespace
{
bool parseString(const QString &ref, const QString &id, QString *result)
{
    if (!ref.startsWith(id)) return false;
    *result = ref.mid(id.size());
    return true;
}

bool parseDouble(const QString &ref, const QString &id, double *result)
{
    bool ok;
    if (!ref.startsWith(id)) return false;
    *result = ref.mid(id.size()).toDouble(&ok);
    return ok;
}

bool parseInt(const QString &ref, const QString &id, int *result)
{
    bool ok;
    if (!ref.startsWith(id)) return false;
    *result = ref.mid(id.size()).toInt(&ok);
    return ok;
}
}  // namespace

void Calibration::save()
{
    QString encoding = serialize();
    Options::setSerializedCalibration(encoding);
    m_Calibration = m_Current;
    qCDebug(KSTARS_EKOS_GUIDE) << QString("Saved calibration: %1").arg(encoding);

}

QString Calibration::serialize() const
{
    QDateTime now = QDateTime::currentDateTime();
    QString dateStr = now.toString("yyyy-MM-dd hh:mm:ss");
    QString raStr = std::isnan(m_Current.MountRA.Degrees()) ? "" : m_Current.MountRA.toDMSString(false, true, true);
    QString decStr = std::isnan(m_Current.MountDEC.Degrees()) ? "" : m_Current.MountDEC.toHMSString(true, true);
    QString s =
        QString("%16,bx=%1,by=%2,pw=%3,ph=%4,fl=%5,ang=%6,angR=%7,angD=%8,"
            "ramspas=%9,decmspas=%10,swap=%11,ra=%12,dec=%13,side=%14,when=%15,calEnd")
        .arg(m_Current.SubBinX).arg(m_Current.SubBinY).arg(m_Current.CCDPixelWidth).arg(m_Current.CCDPixelHeight)
        .arg(m_Current.FocalLength).arg(m_Current.Angle).arg(m_Current.AngleRA)
        .arg(m_Current.AngleDEC).arg(m_Current.PulseRA)
        .arg(m_Current.PulseDEC).arg(m_Current.DecSwap ? 1 : 0)
        .arg(raStr).arg(decStr).arg(static_cast<int>(m_Current.PierSide)).arg(dateStr).arg(CAL_VERSION);
    return s;
}

bool Calibration::restore(ISD::Mount::PierSide PierSide, const dms Declination)
{
    return restore(Options::serializedCalibration(), PierSide, Declination);
}

bool Calibration::restore(const QString &encoding, ISD::Mount::PierSide CurrentPierside,
                          const dms CurrentDeclination)
{
    // Fail if we couldn't read the calibration.
    if (!restore(encoding))
    {
        qCDebug(KSTARS_EKOS_GUIDE) << "Could not restore calibration--couldn't read.";
        return false;
    }
    // We don't restore calibrations where either the calibration or the current pier side
    // is unknown.
    if (m_Calibration.PierSide == ISD::Mount::PIER_UNKNOWN ||
            CurrentPierside == ISD::Mount::PIER_UNKNOWN)
    {
        qCDebug(KSTARS_EKOS_GUIDE) << "Could not restore calibration--pier side unknown.";
        return false;
    }

    if (std::isnan(CurrentDeclination.Degrees()))
    {
        return false;
    }
    // Accept calibration values
    m_Current = m_Calibration;
    setAngle(m_Current.Angle);
    m_initialized = true;
    return true;
}

bool Calibration::restore(const QString &encoding)
{
    QStringList items = encoding.split(',');
    if (items.size() != 17) return false;
    int i = 0;

    bool fixOldCalibration = false;
    if (items[i] == CAL_VERSION_1_0)
    {
        fixOldCalibration = true;
    }
    else if (items[i] == CAL_VERSION) fixOldCalibration = false;
    else return false;
    if (!parseInt(items[++i], "bx=", &m_Calibration.SubBinX)) return false;
    if (!parseInt(items[++i], "by=", &m_Calibration.SubBinY)) return false;
    if (!parseDouble(items[++i], "pw=", &m_Calibration.CCDPixelWidth)) return false;
    if (!parseDouble(items[++i], "ph=", &m_Calibration.CCDPixelHeight)) return false;
    if (!parseDouble(items[++i], "fl=", &m_Calibration.FocalLength)) return false;
    // Mutable parameters
    if (!parseDouble(items[++i], "ang=", &m_Calibration.Angle)) return false;
    if (!parseDouble(items[++i], "angR=", &m_Calibration.AngleRA)) return false;
    if (!parseDouble(items[++i], "angD=", &m_Calibration.AngleDEC)) return false;
    // Switched from storing raPulseMsPerPixel to ...PerArcsecond and similar for DEC.
    // The below allows back-compatibility for older stored calibrations.
    if (!parseDouble(items[++i], "ramspas=", &m_Calibration.PulseRA))
    {
        // Try the old version
        double raPulseMsPerPixel;
        if (parseDouble(items[i], "ramspp=", &raPulseMsPerPixel) && (xArcsecondsPerPixel() > 0))
        {
            // The previous calibration only worked on square pixels.
            m_Calibration.PulseRA = raPulseMsPerPixel / xArcsecondsPerPixel();
        }
        else return false;
    }
    if (!parseDouble(items[++i], "decmspas=", &m_Calibration.PulseDEC))
    {
        // Try the old version
        double decPulseMsPerPixel;
        if (parseDouble(items[i], "decmspp=", &decPulseMsPerPixel) && (yArcsecondsPerPixel() > 0))
        {
            // The previous calibration only worked on square pixels.
            m_Calibration.PulseDEC = decPulseMsPerPixel / yArcsecondsPerPixel();
        }
        else return false;
    }
    int tempInt;
    if (!parseInt(items[++i], "swap=", &tempInt)) return false;
    m_Calibration.DecSwap = static_cast<bool>(tempInt);
    if (fixOldCalibration)
    {
        qCDebug(KSTARS_EKOS_GUIDE) << QString("Modifying v1.0 calibration");
        m_Calibration.DecSwap = !m_Calibration.DecSwap;
    }
    QString tempStr;
    if (!parseString(items[++i], "ra=", &tempStr)) return false;
    dms nullDms;
    m_Calibration.MountRA = tempStr.size() == 0 ? nullDms : dms::fromString(tempStr, true);
    if (!parseString(items[++i], "dec=", &tempStr)) return false;
    m_Calibration.MountDEC = tempStr.size() == 0 ? nullDms : dms::fromString(tempStr, false);
    m_Calibration.MountDEC.reduceToRange(dms::MINUSPI_TO_PI); // Correction for negative DEC!
    if (!parseInt(items[++i], "side=", &tempInt)) return false;
    m_Calibration.PierSide = static_cast<ISD::Mount::PierSide>(tempInt);
    if (!parseString(items[++i], "when=", &tempStr)) return false;
    // Don't keep the time. It's just for reference.
    if (items[++i] != "calEnd") return false;
    return true;
}

double Calibration::correctRAPulse(double raMsPerArcsec, const dms calibrationDec, const dms currentDec)
{
    constexpr double MAX_DEC = 60.0;
    // Don't use uninitialized dms values.
    if (std::isnan(calibrationDec.Degrees()) || std::isnan(currentDec.Degrees()))
    {
        qCDebug(KSTARS_EKOS_GUIDE) << QString("Did not convert calibration RA rate. Input DEC invalid");
        return raMsPerArcsec;
    }
    if ((calibrationDec.Degrees() > MAX_DEC) || (calibrationDec.Degrees() < -MAX_DEC))
    {
        qCDebug(KSTARS_EKOS_GUIDE) << QString("Didn't correct calibration RA rate. Calibration DEC %1 too close to pole")
                                   .arg(QString::number(calibrationDec.Degrees(), 'f', 1));
        return raMsPerArcsec;
    }
    const double toRadians = M_PI / 180.0;
    const double numer = std::cos(currentDec.Degrees() * toRadians);
    const double denom = std::cos(calibrationDec.Degrees() * toRadians);
    if (raMsPerArcsec == 0) return raMsPerArcsec;
    const double rate = 1.0 / raMsPerArcsec;
    if (denom == 0) return raMsPerArcsec;
    const double adjustedRate = numer * rate / denom;
    if (adjustedRate == 0) return raMsPerArcsec;
    const double adjustedMsPerArcsecond = 1.0 / adjustedRate;
    qCDebug(KSTARS_EKOS_GUIDE) << QString("Corrected calibration RA rate. %1 --> %2. Calibration DEC %3 current DEC %4.")
                               .arg(QString::number(raMsPerArcsec, 'f', 1))
                               .arg(QString::number(adjustedMsPerArcsecond, 'f', 1))
                               .arg(QString::number(calibrationDec.Degrees(), 'f', 1))
                               .arg(QString::number(currentDec.Degrees(), 'f', 1));
    return adjustedMsPerArcsecond;
}

double Calibration::getDiffDEC(const dms currentDEC)
{
    return dms(m_Calibration.AngleDEC).deltaAngle(dms(currentDEC)).Degrees();
}

void Calibration::setPierside(const ISD::Mount::PierSide CurrentPierside)
{
    m_Current.PierSide = CurrentPierside;
}

void Calibration::updateRADECAngle(double CamRotation)
{
    setAngle(CamRotation);
    double DiffRotation = dms(CamRotation).deltaAngle(dms(m_Calibration.Angle)).Degrees();
    m_Current.AngleDEC = KSUtils::range360(m_Calibration.AngleDEC + DiffRotation);
    m_Current.AngleRA = KSUtils::range360(m_Calibration.AngleRA + DiffRotation);
}

void Calibration::updateRAPulse(const dms currentDEC)
{
    m_Current.PulseRA = correctRAPulse(m_Calibration.PulseRA, m_Calibration.MountDEC, currentDEC);
}

// TODO: Integrate handling for AltAz!!
// After a flip a subsequent alignment shall be compulsory!
void Calibration::updatePierside(ISD::Mount::PierSide *Pierside, double *Rotation,
                                 FRType *FlipRotReady, const bool NoRotatorDevice)
{
    // updatePerside() is always called from internalguider.cpp with PIER_UNKNOWN
    if (*Pierside == ISD::Mount::PIER_UNKNOWN)
        // RotatorUtils is always activated in Camera::setRotator(), so pierside should be catched
        *Pierside = RotatorUtils::Instance()->getMountPierside();

    if (m_Current.PierSide != *Pierside)
    {
        m_Current.PierSide = *Pierside;
        // With an active rotator device:
        // After a flip and a subsequent align "m_Current.Angle" is already set to the rotated
        // camera PA via newPA() in [Align], hence there is nothing to do.
        if (NoRotatorDevice)
        {
            // With "ManualRotator" or with no rotator(Options::astrometryUseRotator() = false):
            // ManualRotator is bound to [Capture] and [Align] (both technically and logically),
            // while there is no association with [Guide].
            // Because there is no rotator in [Guide] "FlipRotReady->Done" is always false.
            if (!FlipRotReady->Done)
                *Rotation = KSUtils::range360(*Rotation + 180);
            else if (FlipRotReady->Pierside != *Pierside)
                *Rotation = KSUtils::range360(*Rotation + 180);
        }
    }
    FlipRotReady->Done = false;
}

void Calibration::updateDECSwap(ISD::Mount::PierSide CurrentPierside)
{
    m_Current.DecSwap = (CurrentPierside == m_Calibration.PierSide) ? m_Calibration.DecSwap : !m_Calibration.DecSwap;
}

void Calibration::updateBinXY(int CurrentBinX, int CurrentBinY)
{
    m_Current.SubBinX = CurrentBinX;
    m_Current.SubBinY = CurrentBinY;
}
