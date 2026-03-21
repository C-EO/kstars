/*
    SPDX-FileCopyrightText: 2025 KStars Team

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "testksalmanac.h"
#include "../testhelpers.h"

#include "ksalmanac.h"
#include "geolocation.h"
#include "kstarsdatetime.h"
#include "kstarsdata.h"

#include <cmath>

TestKSAlmanac::TestKSAlmanac(QObject *parent) : QObject(parent)
{
}

/**
 * @brief Create a GeoLocation for San Jose, CA (coordinates used by timeanddate.com).
 *        lat = 37.338°N, lng = -121.886°W, TZ = -8 (PST, no DST rule).
 *        The TZ value is irrelevant when UTC KStarsDateTime is passed directly.
 */
static GeoLocation createSanJoseGeo()
{
    return GeoLocation(dms(-121.886), dms(37.338), "San Jose", "California", "USA", -8);
}

/**
 * @brief Create a UTC KStarsDateTime representing local midnight for San Jose.
 * @param date  The local calendar date (San Jose time).
 * @param utcOffsetHours  UTC offset: -7 for PDT (summer), -8 for PST (winter).
 *
 * Example: PDT (UTC-7) midnight = 00:00 PDT = 07:00 UTC → QTime(7, 0, 0).
 *
 * KStarsDateTime defaults to Qt::UTC, so the KSAlmanac constructor uses the
 * value directly without calling LTtoUT, avoiding any KStarsData dependency.
 */
static KStarsDateTime localMidnightUTC(const QDate &date, int utcOffsetHours = -7)
{
    // -utcOffsetHours gives the number of hours to add to reach UTC
    // e.g. UTC-7 → add 7 hours: 00:00 local → 07:00 UTC
    return KStarsDateTime(date, QTime(-utcOffsetHours, 0, 0));
}

void TestKSAlmanac::initTestCase()
{
    qDebug() << "entering";
    // KTEST_BEGIN sets app name to "kstars", enables test mode, and installs
    // data files (VSOP87, city db, star data) so KSPaths::locate finds them.
    KTEST_BEGIN();

    // KSAlmanac relies on KStarsData::Instance() being available for planet
    // position updates (KSPlanetBase::updateCoords calls Instance()->skyComposite()).
    KStarsData *data = KStarsData::Create();
    QVERIFY2(data != nullptr, "KStarsData::Create() returned null");
    QVERIFY2(data->initialize(), "KStarsData::initialize() failed");
    qDebug() << "KStarsData initialized successfully";
}

void TestKSAlmanac::cleanupTestCase()
{
    qDebug() << "entering";
    KTEST_END();
}

void TestKSAlmanac::testDawnDusk_data()
{
    QTest::addColumn<QDate>("date");
    QTest::addColumn<int>("utcOffset");           // e.g. -7 PDT, -8 PST
    QTest::addColumn<double>("expectedDawnHour"); // fraction * 24: local hours from midnight (positive)
    QTest::addColumn<double>("expectedDuskHour"); // fraction * 24: local hours from midnight (negative = prev evening)
    QTest::addColumn<double>("toleranceHours");   // allowed deviation

    // Reference values from timeanddate.com for San Jose, CA (2021).
    // dawn  = "Start" of astronomical twilight for the given date (local time).
    // dusk  = "End"   of astronomical twilight for the PREVIOUS calendar day.
    //
    // dawnHour  = dawn_time_local_hh_mm  (e.g. 03:53 = 3.883h)
    // duskHour  = -(24h - dusk_prev_eve_local_hh_mm)  (e.g. prev-eve 22:25 = -(24 - 22.417) = -1.583h)
    //
    // Tolerance: 5 minutes = 5/60 h (algorithm step ≈ 3 min + interpolation)

    // Jun 21, 2021 — summer solstice (PDT = UTC-7)
    //   Dawn : 03:53 PDT  → dawnHour = 3 + 53/60 = 3.883
    //   Dusk : Jun 20 22:25 PDT → duskHour = -(24 - 22.417) = -1.583
    QTest::newRow("Summer_Jun21_2021")
            << QDate(2021, 6, 21) << -7 << 3.883 << -1.583 << (5.0 / 60.0);

    // Dec 21, 2021 — winter solstice (PST = UTC-8)
    //   Dawn : 05:44 PST  → dawnHour = 5 + 44/60 = 5.733
    //   Dusk : Dec 20 18:27 PST → duskHour = -(24 - 18.450) = -5.550
    QTest::newRow("Winter_Dec21_2021")
            << QDate(2021, 12, 21) << -8 << 5.733 << -5.550 << (5.0 / 60.0);

    // Mar 21, 2021 — spring equinox (PDT = UTC-7, DST started Mar 14)
    //   Dawn : 05:41 PDT  → dawnHour = 5 + 41/60 = 5.683
    //   Dusk : Mar 20 20:47 PDT → duskHour = -(24 - 20.783) = -3.217
    QTest::newRow("Spring_Mar21_2021")
            << QDate(2021, 3, 21) << -7 << 5.683 << -3.217 << (5.0 / 60.0);

    // Sep 21, 2021 — autumnal equinox (PDT = UTC-7)
    //   Dawn : 05:27 PDT  → dawnHour = 5 + 27/60 = 5.450
    //   Dusk : Sep 20 ~20:34 PDT → duskHour = -(24 - 20.567) = -3.433
    QTest::newRow("Autumn_Sep21_2021")
            << QDate(2021, 9, 21) << -7 << 5.450 << -3.433 << (5.0 / 60.0);
}

void TestKSAlmanac::testDawnDusk()
{
    QFETCH(QDate, date);
    QFETCH(int, utcOffset);
    QFETCH(double, expectedDawnHour);
    QFETCH(double, expectedDuskHour);
    QFETCH(double, toleranceHours);

    GeoLocation geo = createSanJoseGeo();
    KStarsDateTime midnight = localMidnightUTC(date, utcOffset);

    // No KStarsData needed: valid UTC KStarsDateTime + non-null GeoLocation
    KSAlmanac almanac(midnight, &geo);

    // fraction * 24 gives local hours from midnight:
    //   dawn > 0  (morning, e.g. 03:53 local = +3.88h)
    //   dusk < 0  (previous evening, e.g. 22:25 local prev day = -1.58h)
    double dawnHour = almanac.getDawnAstronomicalTwilight() * 24.0;
    double duskHour = almanac.getDuskAstronomicalTwilight() * 24.0;

    qDebug() << "Date:" << date.toString(Qt::ISODate)
             << "UTC offset:" << utcOffset
             << "| Dawn:" << dawnHour << "h (expected" << expectedDawnHour << "+/-"
             << (toleranceHours * 60.0) << "min)"
             << "| Dusk:" << duskHour << "h (expected" << expectedDuskHour << ")";

    QVERIFY2(std::abs(dawnHour - expectedDawnHour) <= toleranceHours,
             qPrintable(QString("Dawn %1 h differs from expected %2 h by %3 min (tolerance %4 min)")
                        .arg(dawnHour, 0, 'f', 4)
                        .arg(expectedDawnHour, 0, 'f', 4)
                        .arg(std::abs(dawnHour - expectedDawnHour) * 60.0, 0, 'f', 1)
                        .arg(toleranceHours * 60.0, 0, 'f', 1)));

    QVERIFY2(std::abs(duskHour - expectedDuskHour) <= toleranceHours,
             qPrintable(QString("Dusk %1 h differs from expected %2 h by %3 min (tolerance %4 min)")
                        .arg(duskHour, 0, 'f', 4)
                        .arg(expectedDuskHour, 0, 'f', 4)
                        .arg(std::abs(duskHour - expectedDuskHour) * 60.0, 0, 'f', 1)
                        .arg(toleranceHours * 60.0, 0, 'f', 1)));
}

void TestKSAlmanac::testSchedulerDate2021_06_14()
{
    // June 14, 2021 — San Jose PDT (UTC-7)
    // Reference from timeanddate.com: astronomical dawn = 03:53 PDT = 10:53 UTC.
    // The scheduler originally expected dawn at "10:53 UTC (3:53am PDT)".
    //
    // With localMidnightUTC(Jun 14, -7) = 07:00 UTC:
    //   ksal.getDawnAstronomicalTwilight() * 24 ≈ 3.883 h
    //   ksal.getDate().addSecs(3.883 * 3600) = 07:00 + 03:53 = 10:53 UTC ✓

    GeoLocation geo = createSanJoseGeo();
    const QDate testDate(2021, 6, 14);
    KStarsDateTime midnight = localMidnightUTC(testDate, -7); // PDT

    KSAlmanac almanac(midnight, &geo);

    const double dawnFraction = almanac.getDawnAstronomicalTwilight();
    const double duskFraction = almanac.getDuskAstronomicalTwilight();
    const double dawnHour = dawnFraction * 24.0;
    const double duskHour = duskFraction * 24.0;

    // Local time display (hours from midnight PDT)
    const QTime dawnLocalTime = QTime(0, 0, 0).addSecs(static_cast<int>(std::round(dawnHour * 3600.0)));
    const QTime duskLocalTime = QTime(0, 0, 0).addSecs(static_cast<int>(std::round(duskHour * 3600.0)));

    // UTC time: ksal.getDate() + fraction * 86400 s
    const QDateTime dawnUTC = QDateTime(testDate, QTime(7, 0, 0), QTimeZone::utc())
                              .addSecs(static_cast<int>(std::round(dawnFraction * 86400.0)));

    qDebug() << "";
    qDebug() << "=== KSAlmanac for Jun 14, 2021 (San Jose PDT) ===";
    qDebug() << "ksal.getDate() (UTC) :" << midnight.toString(Qt::ISODate);
    qDebug() << "Dawn fraction        :" << dawnFraction;
    qDebug() << "Dawn local PDT       :" << dawnLocalTime.toString("hh:mm:ss")
             << "(reference: 03:53 PDT from timeanddate.com)";
    qDebug() << "Dawn UTC             :" << dawnUTC.time().toString("hh:mm:ss")
             << "(reference: 10:53 UTC from timeanddate.com)";
    qDebug() << "Dusk fraction        :" << duskFraction;
    qDebug() << "Dusk local PDT       :" << duskLocalTime.toString("hh:mm:ss")
             << "(prev evening, negative offset from midnight)";

    // Assert: dawn ≈ 3.883 h from local midnight (= 03:53 PDT = 10:53 UTC)
    // Reference: timeanddate.com Jun 14, 2021, San Jose.
    const double expectedDawnHour = 3.883; // 03:53 PDT
    const double toleranceHours   = 5.0 / 60.0;

    QVERIFY2(std::abs(dawnHour - expectedDawnHour) <= toleranceHours,
             qPrintable(QString("Dawn %1 h (= %2 PDT) differs from expected %3 h (03:53 PDT) by %4 min")
                        .arg(dawnHour, 0, 'f', 4)
                        .arg(dawnLocalTime.toString("hh:mm"))
                        .arg(expectedDawnHour, 0, 'f', 4)
                        .arg(std::abs(dawnHour - expectedDawnHour) * 60.0, 0, 'f', 1)));
}

void TestKSAlmanac::testDawnDuskBounds_data()
{
    QTest::addColumn<QDate>("date");
    QTest::addColumn<int>("utcOffset");

    // One date per month in 2021 for San Jose.
    // DST 2021: starts Sun Mar 14 02:00 (→ PDT = UTC-7)
    //            ends  Sun Nov  7 02:00 (→ PST = UTC-8)
    QTest::newRow("Jan15_PST") << QDate(2021,  1, 15) << -8;
    QTest::newRow("Feb15_PST") << QDate(2021,  2, 15) << -8;
    QTest::newRow("Mar15_PDT") << QDate(2021,  3, 15) << -7; // DST started Mar 14
    QTest::newRow("Apr15_PDT") << QDate(2021,  4, 15) << -7;
    QTest::newRow("May15_PDT") << QDate(2021,  5, 15) << -7;
    QTest::newRow("Jun15_PDT") << QDate(2021,  6, 15) << -7;
    QTest::newRow("Jul15_PDT") << QDate(2021,  7, 15) << -7;
    QTest::newRow("Aug15_PDT") << QDate(2021,  8, 15) << -7;
    QTest::newRow("Sep15_PDT") << QDate(2021,  9, 15) << -7;
    QTest::newRow("Oct15_PDT") << QDate(2021, 10, 15) << -7;
    QTest::newRow("Nov15_PST") << QDate(2021, 11, 15) << -8; // DST ended Nov 7
    QTest::newRow("Dec15_PST") << QDate(2021, 12, 15) << -8;
}

void TestKSAlmanac::testDawnDuskBounds()
{
    QFETCH(QDate, date);
    QFETCH(int, utcOffset);

    GeoLocation geo = createSanJoseGeo();
    KStarsDateTime midnight = localMidnightUTC(date, utcOffset);

    KSAlmanac almanac(midnight, &geo);

    const double dawnHour = almanac.getDawnAstronomicalTwilight() * 24.0;
    const double duskHour = almanac.getDuskAstronomicalTwilight() * 24.0;

    // At 37.3°N San Jose astronomical twilight always exists year-round:
    //   Dawn is in the morning  → fraction * 24 in (0, 12)
    //   Dusk is previous evening → fraction * 24 in (-12, 0)
    //   Night always exists     → dawnHour > duskHour

    QVERIFY2(dawnHour > 0.0 && dawnHour < 12.0,
             qPrintable(QString("Dawn %1 h is not in expected morning range (0, 12) for %2")
                        .arg(dawnHour, 0, 'f', 3).arg(date.toString("yyyy-MM-dd"))));

    QVERIFY2(duskHour > -12.0 && duskHour < 0.0,
             qPrintable(QString("Dusk %1 h is not in expected prev-evening range (-12, 0) for %2")
                        .arg(duskHour, 0, 'f', 3).arg(date.toString("yyyy-MM-dd"))));

    QVERIFY2(dawnHour > duskHour,
             qPrintable(QString("Dawn %1 h <= Dusk %2 h: no night period found for %3")
                        .arg(dawnHour, 0, 'f', 3).arg(duskHour, 0, 'f', 3)
                        .arg(date.toString("yyyy-MM-dd"))));

    qDebug() << date.toString("yyyy-MM-dd")
             << "dawn:" << QTime(0, 0).addSecs(static_cast<int>(dawnHour * 3600)).toString("hh:mm")
             << "(" << dawnHour << "h)"
             << "dusk (prev eve):"
             << QTime(0, 0).addSecs(static_cast<int>((24.0 + duskHour) * 3600)).toString("hh:mm")
             << "(" << duskHour << "h)";
}

QTEST_MAIN(TestKSAlmanac)
